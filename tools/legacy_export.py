#!/usr/bin/env python3
"""Export an aircraft or static model from the original Echelon (Storm engine) data files.

Reads Data/objects.dat, Data/objects2.dat (statics), Data/gdata.dat,
Data/Graphics/mesh.dat, Data/Graphics/textures.dat and, when a hash is missing
there, Data/Graphics/atextures.dat. Writes:

    <out>/models/<file>_im<N>.model.json   one model per damage state
    <out>/models/<file>_im<N>_<part>.emesh  vertices + indices per part (see write_emesh)
    <out>/textures/<name>.png          DXT1/DXT5 textures decoded to RGBA PNG

The output contains the original game's art and must stay out of the repo.

Container format (magic DATA / MEOS / TEXS / DMAT ...):
    u32 magic, u32 count, u32 dataBytes, u32 nameTableBytes
    count x { u32 nameHash, u32 nameOffset, u32 size, u32 offset }   sorted by hash
    ... record data ...
    name table (NUL-separated cp1251 strings) at the very end of the file

Mesh LOD record "<craft>_<part>_Im<N>_Ld<L>":
    u32 0, u32 numSubsets, u32 numMaterials, u32 1
    f32 sphere x, y, z, radius
    numMaterials x { u32 flags, u32 textureHash, u32 materialHash, u32 0 }
    numSubsets   x { u32 primType(4=trilist), u32 material, u16 firstVertex,
                     u16 vertexCount, u32 indexCount }
    u32 ?, u32 totalVertices, u32 checksum
    u16 indices...
Vertex record "<...>_Ld<L>_V00": totalVertices x { f32 pos[3], f32 normal[3], f32 uv[2] }

Texture record: 56-byte header (u32 size, ?, ?, ?, u32 width, u32 height, ?, ?, ...,
FourCC at byte 48) followed by the mip chain.

Mesh vertices: right-handed, up = -Y, nose = +Z, left = -X, V down, front faces
counter-clockwise around the normals. The exporter rotates them 180 degrees about
X (y -> -y, z -> -z, see to_engine) and flips V, so the runtime needs no fix-ups.
Subset indices are relative to the subset's firstVertex.
Everything else (objects.dat node positions, gdata.dat vectors) is in the classic
left-handed D3D frame (Y up, Z forward): only Z flips (see d3d_to_engine).
Part placement comes from objects.dat nodes (parent-relative), not from gdata.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path


# ---------------------------------------------------------------- containers

@dataclass
class Container:
    data: bytes
    by_name: dict[str, tuple[int, int, int]]  # name -> (hash, offset, size)
    by_hash: dict[int, str]

    @classmethod
    def load(cls, path: Path) -> "Container":
        data = path.read_bytes()
        count, _data_bytes, names_bytes = struct.unpack_from("<III", data, 4)
        names = data[len(data) - names_bytes:]
        by_name: dict[str, tuple[int, int, int]] = {}
        by_hash: dict[int, str] = {}
        for i in range(count):
            h, name_off, size, off = struct.unpack_from("<IIII", data, 16 + i * 16)
            end = names.find(b"\x00", name_off)
            name = names[name_off:end].decode("cp1251", "replace")
            by_name[name] = (h, off, size)
            by_hash[h] = name
        return cls(data, by_name, by_hash)

    def record(self, name: str) -> bytes:
        _h, off, size = self.by_name[name]
        return self.data[off:off + size]

    def __contains__(self, name: str) -> bool:
        return name in self.by_name


# ---------------------------------------------------------------- textures

def _rgb565(c: int) -> tuple[int, int, int]:
    r = (c >> 11) & 0x1F
    g = (c >> 5) & 0x3F
    b = c & 0x1F
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def decode_dxt(block_data: bytes, width: int, height: int, fourcc: bytes) -> list[bytearray]:
    """Decode the first mip of a DXT1/DXT3/DXT5 image into RGBA rows."""
    rows = [bytearray(width * 4) for _ in range(height)]
    bw, bh = (width + 3) // 4, (height + 3) // 4
    block_size = 8 if fourcc == b"DXT1" else 16
    pos = 0
    for by in range(bh):
        for bx in range(bw):
            block = block_data[pos:pos + block_size]
            pos += block_size
            alpha = [255] * 16
            if fourcc == b"DXT5":
                a0, a1 = block[0], block[1]
                bits = int.from_bytes(block[2:8], "little")
                table = [a0, a1]
                if a0 > a1:
                    table += [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                else:
                    table += [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
                alpha = [table[(bits >> (3 * i)) & 7] for i in range(16)]
                block = block[8:]
            elif fourcc == b"DXT3":
                bits = int.from_bytes(block[0:8], "little")
                alpha = [((bits >> (4 * i)) & 15) * 17 for i in range(16)]
                block = block[8:]
            c0, c1 = struct.unpack_from("<HH", block, 0)
            bits = struct.unpack_from("<I", block, 4)[0]
            p0, p1 = _rgb565(c0), _rgb565(c1)
            if fourcc == b"DXT1" and c0 <= c1:
                p2 = tuple((a + b) // 2 for a, b in zip(p0, p1))
                p3 = (0, 0, 0)
                punch = True
            else:
                p2 = tuple((2 * a + b) // 3 for a, b in zip(p0, p1))
                p3 = tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))
                punch = False
            palette = (p0, p1, p2, p3)
            for i in range(16):
                x = bx * 4 + (i & 3)
                y = by * 4 + (i >> 2)
                if x >= width or y >= height:
                    continue
                code = (bits >> (2 * i)) & 3
                r, g, b = palette[code]
                a = 0 if (punch and code == 3) else alpha[i]
                rows[y][x * 4:x * 4 + 4] = bytes((r, g, b, a))
    return rows


def write_png(path: Path, width: int, height: int, rgba_rows: list[bytearray]) -> None:
    def chunk(tag: bytes, payload: bytes) -> bytes:
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(row) for row in rgba_rows)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def export_texture(textures: Container, name: str, out_dir: Path,
                   extras: list[Container] | None = None) -> str | None:
    src: Container | None = textures if name in textures else None
    if src is None:
        for extra in extras or []:
            if name in extra:
                src = extra
                break
    if src is None:
        return None
    rec = src.record(name)
    width, height = struct.unpack_from("<II", rec, 16)
    fourcc = rec[48:52]
    if fourcc not in (b"DXT1", b"DXT3", b"DXT5"):
        print(f"  ! texture {name}: unsupported format {fourcc!r}", file=sys.stderr)
        return None
    rows = decode_dxt(rec[56:], width, height, fourcc)
    out = out_dir / f"{name}.png"
    write_png(out, width, height, rows)
    print(f"  texture {name:24} {width}x{height} {fourcc.decode()} -> {out.name}")
    return out.name


# ---------------------------------------------------------------- meshes

@dataclass
class Material:
    """materials.dat record: D3DMATERIAL7 layout (diffuse, ambient, specular,
    emissive as RGBA floats, then specular power)."""
    name: str = ""
    diffuse: tuple[float, float, float, float] = (1.0, 1.0, 1.0, 1.0)
    specular: tuple[float, float, float] = (0.0, 0.0, 0.0)
    power: float = 0.0

    @classmethod
    def load(cls, mats: Container, mat_hash: int) -> "Material":
        name = mats.by_hash.get(mat_hash)
        if name is None:
            return cls()
        rec = mats.record(name)
        if len(rec) < 68:
            return cls(name)
        f = struct.unpack_from("<17f", rec, 0)
        return cls(name, f[0:4], f[8:11], f[16])


@dataclass
class Subset:
    first_index: int
    index_count: int
    texture: str
    material: Material = field(default_factory=Material)


@dataclass
class PartMesh:
    vertices: list[tuple[float, ...]] = field(default_factory=list)  # 8 floats
    indices: list[int] = field(default_factory=list)
    subsets: list[Subset] = field(default_factory=list)
    # Axis-aligned bounds in the *source* frame (min xyz, max xyz); used to
    # match the part against its node in objects.dat.
    source_bbox: tuple[float, ...] = ()


def to_engine(x: float, y: float, z: float) -> tuple[float, float, float]:
    """mesh.dat vertex frame -> engine frame. Mirrors ech::meshToEngine
    (src/core/include/echelon/math/frame.hpp); keep the two in step.

    Mesh vertices are stored right-handed with up = -Y, nose = +Z, left = -X
    (canopy and pilot's head sit at -Y). That is our frame rotated 180 degrees
    about X, so this is a rotation, not a mirror: triangle winding stays
    unchanged (CCW front)."""
    return x, -y, -z


def d3d_to_engine(x: float, y: float, z: float) -> tuple[float, float, float]:
    """gdata.dat / objects.dat frame -> engine frame. Mirrors ech::d3dToEngine
    (src/core/include/echelon/math/frame.hpp); keep the two in step.

    Everything except mesh vertices (node positions, ViewDelta, hardpoints) is
    in the classic left-handed Direct3D frame: X right, Y up, Z forward. Only Z
    flips. Verified: the glass node lands exactly on the cockpit interior and
    BackViewDelta (0, 1, -6) is above and behind the craft."""
    return x, y, -z


def _faces_normal(tri: tuple[int, int, int], verts: list[tuple[float, ...]]) -> bool:
    """True if the triangle is counter-clockwise around its vertex normals."""
    a, b, c = (verts[i] for i in tri)
    e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    e2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    geo = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
    n = tuple(a[3 + i] + b[3 + i] + c[3 + i] for i in range(3))
    return geo[0] * n[0] + geo[1] * n[1] + geo[2] * n[2] >= 0


def parse_lod(meshes: Container, textures: Container, materials: Container, lod_name: str) -> PartMesh:
    rec = meshes.record(lod_name)
    # Most meshes keep vertices in "<lod>_V00"; some (ha_hvy4) use "<lod>_Vert00".
    vname = next((lod_name + s for s in ("_V00", "_Vert00") if lod_name + s in meshes), None)
    if vname is None:
        raise KeyError(lod_name + "_V00")
    vrec = meshes.record(vname)

    _zero, n_sub, n_mat, _n_vb = struct.unpack_from("<4I", rec, 0)
    mats = [struct.unpack_from("<4I", rec, 32 + i * 16) for i in range(n_mat)]
    off = 32 + n_mat * 16
    subs = []
    for _ in range(n_sub):
        prim, mat, v_start, v_count, i_count = struct.unpack_from("<IIHHI", rec, off)
        if prim != 4:
            raise ValueError(f"{lod_name}: unexpected primitive type {prim}")
        subs.append((mat, v_start, v_count, i_count))
        off += 16
    _a, total_v, _crc = struct.unpack_from("<III", rec, off)
    off += 12
    n_idx = sum(s[3] for s in subs)
    if len(rec) - off != n_idx * 2:
        raise ValueError(f"{lod_name}: index block size mismatch")
    if total_v * 32 != len(vrec):
        raise ValueError(f"{lod_name}: vertex count mismatch")
    raw_idx = struct.unpack_from(f"<{n_idx}H", rec, off)

    pm = PartMesh()
    lo = [float("inf")] * 3
    hi = [float("-inf")] * 3
    for i in range(total_v):
        px, py, pz, nx, ny, nz, u, v = struct.unpack_from("<8f", vrec, i * 32)
        for k, c in enumerate((px, py, pz)):
            lo[k] = min(lo[k], c)
            hi[k] = max(hi[k], c)
        # Source frame: up -Y, nose +Z, left -X; engine: up +Y, nose -Z. See to_engine().
        pm.vertices.append((*to_engine(px, py, pz), *to_engine(nx, ny, nz), u, 1.0 - v))
    pm.source_bbox = (*lo, *hi)

    cursor = 0
    backwards = 0
    for mat, v_start, v_count, i_count in subs:
        # Indices are relative to the subset's first vertex.
        tri = [v_start + i for i in raw_idx[cursor:cursor + i_count]]
        cursor += i_count
        if tri and max(tri) >= v_start + v_count:
            raise ValueError(f"{lod_name}: index outside its subset vertex range")
        first = len(pm.indices)
        for t in range(0, i_count - 2, 3):
            # to_engine() is a rotation, so the original winding is kept as is.
            face = (tri[t], tri[t + 1], tri[t + 2])
            backwards += not _faces_normal(face, pm.vertices)
            pm.indices.extend(face)
        tex_hash = mats[mat][1]
        tex_name = textures.by_hash.get(tex_hash, "")
        pm.subsets.append(Subset(first, len(pm.indices) - first, tex_name, Material.load(materials, mats[mat][2])))
    if backwards:
        print(f"  ! {lod_name}: {backwards} triangles wound against their normals", file=sys.stderr)
    return pm


EMESH_VERSION = 2


def write_emesh(path: Path, pm: PartMesh) -> None:
    """EMSH v2: u32 'EMSH', u32 version, u32 nVerts, u32 nIndices, u32 nSubsets,
    nSubsets x { u32 firstIndex, u32 indexCount, char texture[64], char material[64],
                 f32 diffuse[4], f32 specular[3], f32 power },
    nVerts x 8 f32 (pos, normal, uv), nIndices x u32."""
    with path.open("wb") as f:
        f.write(b"EMSH")
        f.write(struct.pack("<IIII", EMESH_VERSION, len(pm.vertices), len(pm.indices), len(pm.subsets)))
        for s in pm.subsets:
            f.write(struct.pack("<II", s.first_index, s.index_count))
            f.write(s.texture.encode("ascii", "replace")[:63].ljust(64, b"\x00"))
            f.write(s.material.name.encode("ascii", "replace")[:63].ljust(64, b"\x00"))
            f.write(struct.pack("<8f", *s.material.diffuse, *s.material.specular, s.material.power))
        for v in pm.vertices:
            f.write(struct.pack("<8f", *v))
        f.write(struct.pack(f"<{len(pm.indices)}I", *pm.indices))


# ---------------------------------------------------------------- hierarchy

@dataclass
class PartNode:
    name: str          # ObjectName ("LE") or "HULL"
    position: tuple[float, float, float]
    children: list["PartNode"] = field(default_factory=list)


def parse_craft_hull(gdata: bytes, craft: str, kind: str = "Craft",
                     window: int = 20000) -> tuple[str, PartNode]:
    """Returns (mesh file name, root part) from a Craft("...") or Static("...") block.

    `window` bounds how much of gdata is scanned after the opening token. Crafts
    fit in the default; a static with a deep Part tree needs a larger window.
    """
    start = gdata.find(f'{kind}("{craft}")'.encode())
    if start < 0:
        raise KeyError(f"{kind} {craft!r} not found in gdata.dat")
    text = gdata[start:start + window].decode("cp1251", "replace")
    root_m = re.search(r'Root\("([^"]+)"\)\s*\{', text)
    if not root_m:
        raise ValueError(f"{kind} {craft}: no Root block")
    file_m = re.search(r'FileName\s*=\s*"([^"]+)"', text[root_m.end():])
    mesh_file = file_m.group(1) if file_m else craft

    # Walk the brace structure after Root(.
    root = PartNode("HULL", (0.0, 0.0, 0.0))
    stack = [root]
    pos = root_m.end()
    depth = 1
    part_re = re.compile(r'Part\("([^"]+)"\)\s*\{')
    while depth > 0 and pos < len(text):
        nxt_part = part_re.search(text, pos)
        nxt_open = text.find("{", pos)
        nxt_close = text.find("}", pos)
        if nxt_close < 0:
            break
        if nxt_part and nxt_part.start() < nxt_close:
            body_start = nxt_part.end()
            body_end = text.find("}", body_start)
            body = text[body_start:body_end]
            pm = re.search(r"Position\s*=\s*\(([^)]*)\)", body)
            position = tuple(float(x) for x in pm.group(1).split(",")) if pm else (0.0, 0.0, 0.0)
            om = re.search(r'ObjectName\s*=\s*"([^"]+)"', body)
            node = PartNode(om.group(1) if om else nxt_part.group(1), position)
            stack[-1].children.append(node)
            stack.append(node)
            depth += 1
            pos = nxt_part.end()
        elif nxt_open != -1 and nxt_open < nxt_close:
            depth += 1
            pos = nxt_open + 1
        else:
            depth -= 1
            stack.pop() if len(stack) > 1 else None
            pos = nxt_close + 1
    return mesh_file, root


# ---------------------------------------------------------------- objects.dat nodes

@dataclass
class ObjectNode:
    """A sub-object node from the craft's objects.dat record.

    Node blocks are laid out as: N damage-state entries of 40 bytes
    { f32 bbox min[3], max[3], f32 radius, u32 x3 }, then { f32 position[3],
    f32 forward[3], f32 up[3], u32 } and an 8-byte name/hash tag. Position and
    orientation are relative to the parent (the parent comes from the mesh
    name: "LE_LW" hangs off "LE"). Forward/up are not always the identity:
    wings are often banked, so any orthonormal pair is accepted."""
    offset: int
    position: tuple[float, float, float]
    forward: tuple[float, float, float]
    up: tuple[float, float, float]
    bboxes: list[tuple[float, ...]]
    tag: bytes


def _cross(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, float, float]:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _mul_vec(cols: tuple[tuple[float, ...], ...], v: tuple[float, ...]) -> tuple[float, float, float]:
    """cols are the columns of a 3x3."""
    return tuple(sum(cols[j][i] * v[j] for j in range(3)) for i in range(3))


def _mul_mat(a: tuple[tuple[float, ...], ...], b: tuple[tuple[float, ...], ...]) -> tuple[tuple[float, float, float], ...]:
    """Matrix product; both arguments and the result are stored as columns."""
    return tuple(_mul_vec(a, b[j]) for j in range(3))


def _node_basis(node: ObjectNode) -> tuple[tuple[float, float, float], ...]:
    """Columns of the D3D rotation: local (right, up, forward) -> parent."""
    right = _cross(node.up, node.forward)
    return (right, node.up, node.forward)


def _d3d_rotation_to_engine(cols: tuple[tuple[float, ...], ...]) -> tuple[tuple[float, float, float], ...]:
    """Conjugates a D3D rotation by the Z flip (x, y, z) -> (x, y, -z).
    (D R D)_ij = s_i R_ij s_j with s = (1, 1, -1)."""
    out = []
    for j in range(3):
        col = cols[j]
        sign = -1.0 if j == 2 else 1.0
        out.append((sign * col[0], sign * col[1], -sign * col[2]))
    return tuple(out)


def _quat_from_columns(cols: tuple[tuple[float, ...], ...]) -> tuple[float, float, float, float]:
    """Rotation quaternion (x, y, z, w) from matrix columns."""
    m = [[cols[j][i] for j in range(3)] for i in range(3)]
    trace = m[0][0] + m[1][1] + m[2][2]
    if trace > 0.0:
        s = (trace + 1.0) ** 0.5 * 2.0
        return ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s)
    if m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = (1.0 + m[0][0] - m[1][1] - m[2][2]) ** 0.5 * 2.0
        return (0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    if m[1][1] > m[2][2]:
        s = (1.0 + m[1][1] - m[0][0] - m[2][2]) ** 0.5 * 2.0
        return ((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    s = (1.0 + m[2][2] - m[0][0] - m[1][1]) ** 0.5 * 2.0
    return ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s)


def mesh_bbox_d3d(pm: PartMesh) -> tuple[float, ...]:
    """Mesh bounds are stored with Y down; node bboxes are Y up."""
    lo, hi = pm.source_bbox[:3], pm.source_bbox[3:]
    return (lo[0], -hi[1], lo[2], hi[0], -lo[1], hi[2])


def parse_object_nodes(objects: Container, name: str, pos_limit: float = 40.0,
                       bbox_limit: float = 80.0) -> list[ObjectNode]:
    """Scan a MEOS record for part nodes.

    The default limits match aircraft (part offsets stay inside 40 m, part
    bboxes inside 80 m). Statics — a 600 m bridge, a radar complex — need both
    limits raised; callers that do so pass the wider values explicitly.
    """
    if name not in objects:
        return []
    rec = objects.record(name)
    nodes: list[ObjectNode] = []

    def length(v: tuple[float, ...]) -> float:
        return sum(c * c for c in v) ** 0.5

    last = -100
    for off in range(0, len(rec) - 48, 4):
        pos = struct.unpack_from("<3f", rec, off)
        fwd = struct.unpack_from("<3f", rec, off + 12)
        up = struct.unpack_from("<3f", rec, off + 24)
        fl, ul = length(fwd), length(up)
        if not (0.97 < fl < 1.03 and 0.97 < ul < 1.03):
            continue
        if abs(sum(a * b for a, b in zip(fwd, up))) > 0.05:
            continue
        if any(abs(c) > pos_limit for c in pos):
            continue
        # A node and the same floats read 4 bytes later both look orthonormal.
        if off - last == 4:
            continue
        bboxes = []
        p = off - 40
        while p >= 0 and len(bboxes) < 8:
            e = struct.unpack_from("<7f3I", rec, p)
            if not all(abs(v) < bbox_limit for v in e[:7]) or e[9] >= 100000:
                break
            if any(abs(v) > 1e-6 for v in e[:6]) and e[6] > 0:
                bboxes.append(e[:6])
            p -= 40
        if not bboxes:
            continue
        nodes.append(ObjectNode(off, pos, fwd, up, bboxes, rec[off + 40:off + 48]))
        last = off
    return nodes


def assign_nodes(nodes: list[ObjectNode], parts: list[tuple[str, PartMesh]]) -> dict[str, ObjectNode]:
    """Greedy one-to-one match of parts to nodes by bbox.

    Mirrored parts share a bbox; the sign of the node's X breaks the tie
    (left parts sit at negative X in the source frame)."""
    scored: list[tuple[float, int, str, int]] = []
    for sub, pm in parts:
        want = mesh_bbox_d3d(pm)
        leaf = sub.split("_")[-1].upper()
        want_left = leaf.startswith("L")
        for i, node in enumerate(nodes):
            dist = min(max(abs(a - b) for a, b in zip(bb, want)) for bb in node.bboxes)
            side = 0
            if abs(node.position[0]) >= 0.5 and (node.position[0] < 0) != want_left:
                side = 1
            scored.append((dist, side, sub, i))
    scored.sort()
    used_parts: set[str] = set()
    used_nodes: set[int] = set()
    out: dict[str, ObjectNode] = {}
    for dist, _side, sub, i in scored:
        if dist > 0.25 or sub in used_parts or i in used_nodes:
            continue
        out[sub] = nodes[i]
        used_parts.add(sub)
        used_nodes.add(i)
    return out


# objects2.dat root parent sentinel (not a container name hash).
O2_ROOT = 0xFF5F5C6C


@dataclass
class O2Node:
    """One node of an objects2.dat record.

    A mesh node is 200 bytes and its name hash is the container hash of the
    mesh stub `<file>[_part]_Im0`. A 48-byte anchor has no name (name_hash is
    None) and carries only a transform. `extra` on a mesh node counts the
    anchors that follow it; this parser does not need that count because the
    two sizes are told apart by the record itself.
    """
    parent: int
    position: tuple[float, float, float]
    forward: tuple[float, float, float]
    up: tuple[float, float, float]
    name_hash: int | None


def _is_unit_pair(fwd: tuple[float, ...], up: tuple[float, ...]) -> bool:
    def length(v: tuple[float, ...]) -> float:
        return sum(c * c for c in v) ** 0.5

    fl, ul = length(fwd), length(up)
    if not (0.97 < fl < 1.03 and 0.97 < ul < 1.03):
        return False
    return abs(sum(a * b for a, b in zip(fwd, up))) < 0.05


def parse_objects2_nodes(rec: bytes) -> list[O2Node]:
    """Walk a packed objects2 record: 200-byte mesh nodes and 48-byte anchors.

    Verified by consuming every record in the Wind Warriors file exactly.
    """
    nodes: list[O2Node] = []
    off = 0
    n = len(rec)
    while off + 48 <= n:
        fwd = struct.unpack_from("<3f", rec, off + 24)
        up = struct.unpack_from("<3f", rec, off + 36)
        if not _is_unit_pair(fwd, up):
            break
        parent = struct.unpack_from("<I", rec, off + 4)[0]
        pos = struct.unpack_from("<3f", rec, off + 12)
        name_hash: int | None = None
        step = 48
        if off + 200 <= n:
            h1, h2 = struct.unpack_from("<2I", rec, off + 48)
            nxt = off + 200
            nxt_ok = nxt == n or (
                nxt + 48 <= n and _is_unit_pair(
                    struct.unpack_from("<3f", rec, nxt + 24),
                    struct.unpack_from("<3f", rec, nxt + 36)))
            if h1 == h2 and nxt_ok:
                name_hash = h1
                step = 200
        if step == 48:
            nxt = off + 48
            nxt_ok = nxt == n or (
                nxt + 48 <= n and _is_unit_pair(
                    struct.unpack_from("<3f", rec, nxt + 24),
                    struct.unpack_from("<3f", rec, nxt + 36)))
            if not nxt_ok:
                break
        nodes.append(O2Node(parent, pos, fwd, up, name_hash))
        off += step
    return nodes


def _same_abs_components(a: tuple[float, ...], b: tuple[float, ...], tol: float = 0.05) -> bool:
    """True when the two vectors hold the same absolute components, any order.

    objects2 stores the same point as the objects.dat node, but the axis order
    is not one fixed permutation (it follows the node). Matching on the sorted
    absolute values finds that node without inventing a conversion.
    """
    aa = sorted(abs(c) for c in a)
    bb = sorted(abs(c) for c in b)
    return all(abs(x - y) <= tol for x, y in zip(aa, bb))


def bind_objects2_fallbacks(mesh_file: str, meshes: Container, objects2: Container,
                            parts: list[tuple[str, PartMesh]], d3d_nodes: list[ObjectNode],
                            assigned: dict[str, ObjectNode]) -> None:
    """Name-match parts that bbox matching missed.

    The objects2 node is identified by the container hash of `<file>[_part]_Im0`.
    Its position is then tied to the unique objects.dat node with the same
    absolute components, and that D3D node (position and basis) is used.
    """
    if mesh_file not in objects2:
        return
    by_hash = {n.name_hash: n for n in parse_objects2_nodes(objects2.record(mesh_file))
               if n.name_hash not in (None, 0xFFFFFFFF)}
    for sub, _pm in parts:
        if sub in assigned:
            continue
        stub = f"{mesh_file}_Im0" if sub == HULL_PART else f"{mesh_file}_{sub}_Im0"
        if stub not in meshes.by_name:
            continue
        o2 = by_hash.get(meshes.by_name[stub][0])
        if o2 is None:
            continue
        hits = [n for n in d3d_nodes if _same_abs_components(n.position, o2.position)]
        if len(hits) != 1:
            print(f"  ! {sub}: objects2 node matches {len(hits)} objects.dat nodes", file=sys.stderr)
            continue
        # The name tie can land on a hardpoint whose position numbers coincide but
        # whose bbox is a different volume. Reject those; a real part stays within
        # a few metres (tower hull was 17 m, a swapped hangar pivot was 60 m+).
        want = mesh_bbox_d3d(_pm)
        dist = min(max(abs(a - b) for a, b in zip(bb, want)) for bb in hits[0].bboxes)
        if dist > 20.0:
            print(f"  ! {sub}: objects2 tie bbox off by {dist:.1f} m, leaving the part at the origin",
                  file=sys.stderr)
            continue
        assigned[sub] = hits[0]
        print(f"  {sub}: placed from objects2 name via objects.dat node (bbox {dist:.1f} m)")


def _static_display_for_file(text: str, filename: str) -> str | None:
    for m in re.finditer(r'Static\("([^"]+)"\)', text):
        window = text[m.end():m.end() + 12000]
        nxt = re.search(r"\n(?:Static|Craft|Road|Vehicle|Turret)\(", window)
        block = window[:nxt.start()] if nxt else window
        fm = re.search(r'FileName\s*=\s*"([^"]+)"', block)
        if fm and fm.group(1) == filename:
            return m.group(1)
    return None


def list_statics(gdata: bytes) -> list[str]:
    return [m.group(1) for m in re.finditer(r'Static\("([^"]+)"\)', gdata.decode("cp1251", "replace"))]


HIDDEN_BY_DEFAULT = {"CoPilot"}


@dataclass
class Sources:
    gdata: bytes
    meshes: Container
    textures: Container
    materials: Container
    objects: Container


def load_sources(data_dir: Path) -> Sources:
    return Sources(
        (data_dir / "gdata.dat").read_bytes(),
        Container.load(data_dir / "Graphics" / "mesh.dat"),
        Container.load(data_dir / "Graphics" / "textures.dat"),
        Container.load(data_dir / "Graphics" / "materials.dat"),
        Container.load(data_dir / "objects.dat"),
    )


def list_crafts(gdata: bytes) -> list[str]:
    return [m.group(1) for m in re.finditer(r'Craft\("([^"]+)"\)', gdata.decode("cp1251", "replace"))]


# Some hulls are stored as "<file>_Im<d>_Ld<l>" with no part infix (BF-2, SF-1).
HULL_PART = "HULL"


def _record_name(mesh_file: str, sub: str, damage: int, lod: int) -> str:
    if sub == HULL_PART:
        return f"{mesh_file}_Im{damage}_Ld{lod}"
    return f"{mesh_file}_{sub}_Im{damage}_Ld{lod}"


def _subs_at(meshes: Container, mesh_file: str, lod: int, damage: int) -> list[str]:
    """Part names that have an <file>_<part>_Im<damage>_Ld<lod> record."""
    prefix = mesh_file + "_"
    suffix = f"_Im{damage}_Ld{lod}"
    subs: list[str] = []
    if f"{mesh_file}{suffix}" in meshes:
        subs.append(HULL_PART)
    for n in meshes.by_name:
        # Vertex blobs are separate records ("..._V00") and are not parts.
        if "_V" in n or not (n.startswith(prefix) and n.endswith(suffix)):
            continue
        sub = n[len(prefix):-len(suffix)]
        if sub:
            subs.append(sub)
    return sorted(subs)


def _damage_states(meshes: Container, mesh_file: str, lod: int) -> list[int]:
    prefix = mesh_file + "_"
    tail = f"_Ld{lod}"
    states: set[int] = set()
    for n in meshes.by_name:
        if "_V" in n or not (n.startswith(prefix) and n.endswith(tail)):
            continue
        mid = n[len(prefix):-len(tail)]
        # Part records look like "<part>_Im<d>"; the hull record is just "Im<d>".
        m = re.search(r"_Im(\d+)$", mid) or re.fullmatch(r"Im(\d+)", mid)
        if m:
            states.add(int(m.group(1)))
    return sorted(states)


def export_craft(data_dir: Path, craft: str, out_dir: Path, lod: int = 0,
                 damage: int | None = None, sources: Sources | None = None,
                 exported_textures: set[str] | None = None, *,
                 entity: str = "Craft", json_key: str = "craft",
                 pos_limit: float = 40.0, bbox_limit: float = 80.0,
                 window: int = 20000, objects2: Container | None = None,
                 extra_textures: list[Container] | None = None,
                 mesh_override: str | None = None) -> bool:
    """Writes one model per damage state. Returns False when the unit has no mesh.

    `damage` restricts the export to a single Im state; None writes every state
    present in mesh.dat. Placement is resolved once, from the intact mesh, and
    reused: a damaged mesh has a different bbox and would miss its node.

    Aircraft keep the default limits and `json_key="craft"`. Statics pass wider
    limits, `entity="Static"`, `json_key="static"` and the objects2 container
    used when a bbox match fails. `mesh_override` exports a mesh file that has
    no Static() block (bridge sections).
    """
    print(f"== {craft}")
    src = sources or load_sources(data_dir)
    if exported_textures is None:
        exported_textures = set()

    if mesh_override:
        mesh_file, root = mesh_override, PartNode("HULL", (0.0, 0.0, 0.0))
    else:
        try:
            mesh_file, root = parse_craft_hull(src.gdata, craft, entity, window)
        except (KeyError, ValueError) as e:
            print(f"  skip: {e}")
            return False
    states = [damage] if damage is not None else _damage_states(src.meshes, mesh_file, lod)
    states = [d for d in states if _subs_at(src.meshes, mesh_file, lod, d)]
    if not states:
        print(f"  skip: no mesh records for {mesh_file}")
        return False
    print(f"  mesh file {mesh_file}, damage states {states}")
    # A bare mesh (bridge section) is one root part at the origin. Its objects.dat
    # record still contains float triples that pass the orthonormal test and a
    # bbox match, which shifted riverbridge200m_part by 259 m along Z.
    nodes = [] if mesh_override else parse_object_nodes(src.objects, mesh_file, pos_limit, bbox_limit)

    models_dir = out_dir / "models"
    tex_dir = out_dir / "textures"
    models_dir.mkdir(parents=True, exist_ok=True)
    tex_dir.mkdir(parents=True, exist_ok=True)

    # gdata Position values are only a fallback: they are hit-point offsets,
    # not mesh placement (objects.dat nodes are).
    gdata_offsets: dict[str, tuple[float, float, float]] = {}

    def walk(node: PartNode) -> None:
        gdata_offsets[node.name] = node.position
        for c in node.children:
            walk(c)

    walk(root)

    # Local (parent-relative) placement in the D3D frame, keyed by sub name.
    # Resolved from the intact mesh: damaged geometry has a different bbox.
    ident = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    local_pos: dict[str, tuple[float, float, float]] = {}
    local_rot: dict[str, tuple[tuple[float, float, float], ...]] = {}
    base = 0 if _subs_at(src.meshes, mesh_file, lod, 0) else states[0]
    base_subs = _subs_at(src.meshes, mesh_file, lod, base)
    base_parts = [(sub, parse_lod(src.meshes, src.textures, src.materials,
                                   _record_name(mesh_file, sub, base, lod)))
                  for sub in base_subs]
    assigned = assign_nodes(nodes, base_parts)
    if objects2 is not None and not mesh_override:
        bind_objects2_fallbacks(mesh_file, src.meshes, objects2, base_parts, nodes, assigned)
    for sub, _pm in base_parts:
        node = assigned.get(sub)
        if node is not None:
            local_pos[sub] = node.position
            local_rot[sub] = _node_basis(node)
        else:
            leaf = sub.split("_")[-1]
            local_pos[sub] = gdata_offsets.get(leaf, (0.0, 0.0, 0.0))
            local_rot[sub] = ident
            if not mesh_override:
                print(f"  ! {sub}: no objects.dat node matched, using gdata offset", file=sys.stderr)

    def absolute_transform(sub: str) -> tuple[tuple[float, float, float], tuple[tuple[float, float, float], ...]]:
        """Parent-relative D3D transforms composed out to the model root."""
        chain: list[str] = []
        p: str | None = sub
        while p:
            chain.append(p)
            p = p.rsplit("_", 1)[0] if "_" in p else ""
        pos = (0.0, 0.0, 0.0)
        rot = ident
        for name in reversed(chain):
            lp = local_pos.get(name, (0.0, 0.0, 0.0))
            lr = local_rot.get(name, ident)
            pos = tuple(pos[k] + _mul_vec(rot, lp)[k] for k in range(3))
            rot = _mul_mat(rot, lr)
        return pos, rot

    for state in states:
        parts_json = []
        bound_min = [float("inf")] * 3
        bound_max = [float("-inf")] * 3
        tri_total = 0
        for sub in _subs_at(src.meshes, mesh_file, lod, state):
            try:
                pm = parse_lod(src.meshes, src.textures, src.materials,
                               _record_name(mesh_file, sub, state, lod))
            except KeyError as e:
                print(f"  ! {sub} im{state}: missing record {e}", file=sys.stderr)
                continue
            parent = sub.rsplit("_", 1)[0] if "_" in sub else ""
            abs_pos, abs_rot = absolute_transform(sub)
            ox, oy, oz = d3d_to_engine(*abs_pos)
            qx, qy, qz, qw = _quat_from_columns(_d3d_rotation_to_engine(abs_rot))
            emesh = models_dir / f"{mesh_file}_im{state}_{sub}.emesh"
            write_emesh(emesh, pm)
            tri_total += len(pm.indices) // 3
            if json_key == "static":
                rot_e = _d3d_rotation_to_engine(abs_rot)
                for v in pm.vertices:
                    p = _mul_vec(rot_e, v[:3])
                    for k in range(3):
                        c = p[k] + (ox, oy, oz)[k]
                        bound_min[k] = min(bound_min[k], c)
                        bound_max[k] = max(bound_max[k], c)
            for s in pm.subsets:
                if s.texture and s.texture not in exported_textures:
                    if export_texture(src.textures, s.texture, tex_dir, extra_textures):
                        exported_textures.add(s.texture)
            parts_json.append({
                "name": sub,
                "mesh": emesh.name,
                "parent": parent,
                "position": [ox, oy, oz],  # absolute, in the model root frame
                "orientation": [qx, qy, qz, qw],  # absolute, (x, y, z, w)
                "local_position": list(d3d_to_engine(*local_pos.get(sub, (0.0, 0.0, 0.0)))),
                "textures": sorted({s.texture for s in pm.subsets if s.texture}),
                "materials": sorted({s.material.name for s in pm.subsets if s.material.name}),
                # Sub-objects the original engine toggles by script; hidden in the
                # intact state. CoPilot is the ejected pilot on his glider (it has
                # an extra Im3 state).
                "visible": sub.split("_")[-1].casefold() not in {n.casefold() for n in HIDDEN_BY_DEFAULT},
            })
        model = {
            json_key: craft,
            "source": mesh_file,
            "lod": lod,
            "damage_state": state,
            "parts": parts_json,
        }
        out_json = models_dir / f"{mesh_file}_im{state}.model.json"
        out_json.write_text(json.dumps(model, indent=2), encoding="utf-8")
        print(f"  im{state}: {len(parts_json)} parts, {tri_total} tris -> {out_json.name}")
        if json_key == "static" and bound_min[0] <= bound_max[0]:
            size = tuple(round(bound_max[k] - bound_min[k], 1) for k in range(3))
            print(f"  im{state} engine size (m) xyz={size}")
    return True


def _load_optional(path: Path) -> Container | None:
    if not path.is_file():
        return None
    return Container.load(path)


def export_static(data_dir: Path, name: str, out_dir: Path, lod: int = 0,
                  damage: int | None = None, sources: Sources | None = None,
                  exported_textures: set[str] | None = None,
                  objects2: Container | None = None,
                  extra_textures: list[Container] | None = None) -> bool:
    """Export one static. `name` is a Static("...") title or a mesh FileName.

    A FileName with no Static block (a bridge section) is exported as a single
    mesh. Placement uses objects.dat in the D3D frame, with objects2 only to
    recognise a part whose bbox did not match.
    """
    src = sources or load_sources(data_dir)
    o2 = objects2 if objects2 is not None else Container.load(data_dir / "objects2.dat")
    if extra_textures is None:
        atex = _load_optional(data_dir / "Graphics" / "atextures.dat")
        extra_textures = [atex] if atex is not None else []
    text = src.gdata.decode("cp1251", "replace")
    common = dict(lod=lod, damage=damage, sources=src, exported_textures=exported_textures,
                  entity="Static", json_key="static", pos_limit=4000.0, bbox_limit=4000.0,
                  window=100000, objects2=o2, extra_textures=extra_textures)
    if f'Static("{name}")' in text:
        return export_craft(data_dir, name, out_dir, **common)
    display = _static_display_for_file(text, name)
    if display:
        return export_craft(data_dir, display, out_dir, **common)
    # No Static block: still a mesh file (bridge section, shell, ...).
    if not _subs_at(src.meshes, name, lod, 0) and not _damage_states(src.meshes, name, lod):
        print(f"== {name}")
        print(f"  skip: no Static(\"{name}\") and no mesh records")
        return False
    return export_craft(data_dir, name, out_dir, mesh_override=name, **common)


def export_all_statics(data_dir: Path, out_dir: Path, lod: int, damage: int | None) -> None:
    src = load_sources(data_dir)
    o2 = Container.load(data_dir / "objects2.dat")
    atex = _load_optional(data_dir / "Graphics" / "atextures.dat")
    extras = [atex] if atex is not None else []
    textures: set[str] = set()
    done = skipped = 0
    for name in list_statics(src.gdata):
        try:
            if export_static(data_dir, name, out_dir, lod, damage, src, textures, o2, extras):
                done += 1
            else:
                skipped += 1
        except Exception as e:
            skipped += 1
            print(f"  skip: {name}: {e}", file=sys.stderr)
    print(f"exported {done}, skipped {skipped}")


def export_all(data_dir: Path, out_dir: Path, lod: int, damage: int | None) -> None:
    src = load_sources(data_dir)
    textures: set[str] = set()
    done = skipped = 0
    for craft in list_crafts(src.gdata):
        try:
            if export_craft(data_dir, craft, out_dir, lod, damage, src, textures):
                done += 1
            else:
                skipped += 1
        except Exception as e:  # one bad craft must not abort the batch
            skipped += 1
            print(f"  skip: {craft}: {e}", file=sys.stderr)
    print(f"exported {done}, skipped {skipped}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--data", type=Path, default=Path("legacy/Echelon Wind Warriors/Data"),
                    help="original Data directory (default: legacy/Echelon Wind Warriors/Data)")
    ap.add_argument("--out", type=Path, default=Path("assets/legacy"),
                    help="output directory (default: assets/legacy, git-ignored)")
    ap.add_argument("--craft", default=None,
                    help="Craft(\"...\") name in gdata.dat, or 'all' (default: Human_BF1 when --static is absent)")
    ap.add_argument("--static", default=None,
                    help="Static(\"...\") title, mesh FileName, or 'all'")
    ap.add_argument("--lod", type=int, default=0)
    ap.add_argument("--damage", type=int, default=None,
                    help="only this Im state (default: every state the mesh has)")
    args = ap.parse_args()
    if args.static and args.craft:
        ap.error("--static and --craft are mutually exclusive")
    if args.static:
        if args.static == "all":
            export_all_statics(args.data, args.out, args.lod, args.damage)
        else:
            export_static(args.data, args.static, args.out, args.lod, args.damage)
        return 0
    craft = args.craft or "Human_BF1"
    if craft == "all":
        export_all(args.data, args.out, args.lod, args.damage)
    else:
        export_craft(args.data, craft, args.out, args.lod, args.damage)
    return 0


if __name__ == "__main__":
    sys.exit(main())
