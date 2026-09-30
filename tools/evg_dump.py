#!/usr/bin/env python3
"""Dump an EVG1 container (missions, scatter, menus) from Echelon: Wind Warriors.

The layout is the one UniVars.dll loads (checked against NetArena.ros,
AICommands.dsc and Instant Action.gsl). See docs/specs/evg.md.

    python tools/evg_dump.py "legacy/Echelon Wind Warriors/Data/Scenes/NetArena.ros"
    python tools/evg_dump.py --json --depth 2 path/to/file.gsd

Field names are cp1251 C strings. The integer values of Name, Type and
Layout1..4 are CRC32 hashes of cp1251 names (zlib.crc32 with the final xor
undone), the same function the DATA container uses for record names. A reverse
dictionary is built from gdata.dat and from identifiers in nearby DLLs and
.ai files; an unknown hash is printed as 0x........ . 0 and -1 stay integers.
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

CLASS_NAME = {
    0x189596B9: "int",
    0x3FCFC71D: "flt",
    0x2AF9C65D: "v3f",
    0xAF293CAF: "txt",
    0x75710EAA: "bin",
    0x9E045FA0: "ref",
    0x5766773F: "arr",
    0xF705FAA8: "ctr",
}
NULL_CLASS = 0xFFFFFFFF
# Fields whose int payload is the DATA/UniVars name hash (CRC32, no final xor).
HASH_FIELDS = {"Name", "Layout1", "Layout2", "Layout3", "Layout4", "Type"}
_NAME_CACHE: dict[str, dict[int, str]] = {}
_QUOTED = re.compile(rb'"([^"\r\n]{2,80})"')
_IDENT = re.compile(rb"\b([A-Za-z_][A-Za-z0-9_ ]{1,60})\b")


@dataclass
class Node:
    type: str
    name: str | None = None
    value: object = None
    children: list["Node"] = field(default_factory=list)
    # Array holes stay in `children` as type "null" so indexes are stable.
    block: int | None = None
    note: str | None = None
    resolved: str | None = None


@dataclass
class EvgFile:
    path: Path
    data: bytes
    index: list[tuple[int, int]]  # (offset, size) into the data section
    body: bytes
    problems: list[str] = field(default_factory=list)
    names: dict[int, str] = field(default_factory=dict)

    @property
    def block_count(self) -> int:
        return len(self.index)

    def payload(self, index: int) -> bytes | None:
        """Bytes UniVars hands to a typed reader: size bytes at offset+8."""
        if index < 0 or index >= len(self.index):
            self.problems.append(f"block {index} out of range")
            return None
        off, size = self.index[index]
        if size == 0:
            return b""
        start = off + 8
        end = start + size
        if off < 0 or start < 0 or end > len(self.body):
            self.problems.append(
                f"block {index} payload [{start}:{end}] outside data section "
                f"({len(self.body)} bytes)"
            )
            return None
        return self.body[start:end]


def crc32_name(raw: bytes) -> int:
    """CRC32 of `raw` with the usual final complement removed.

    UniVars stores this in each .ctr child (init 0xFFFFFFFF, no final xor).
    The same value is the DATA/MEOS record-name hash.
    """
    return (zlib.crc32(raw) & 0xFFFFFFFF) ^ 0xFFFFFFFF


def _remember(table: dict[int, str], raw: bytes, *, overwrite: bool) -> None:
    if not (2 <= len(raw) <= 80):
        return
    if any(b < 0x20 for b in raw):
        return
    text = raw.decode("cp1251", "replace")
    digest = crc32_name(raw)
    if overwrite or digest not in table:
        table[digest] = text


def _harvest(table: dict[int, str], blob: bytes, *, overwrite: bool) -> None:
    """Hash quoted strings and identifiers. Raw binary runs are skipped.

    A free scan of every printable span collides with real names often enough
    to print the wrong label. Quoted text and identifiers are what gdata.dat
    and the DLLs actually store.
    """
    for match in _QUOTED.finditer(blob):
        _remember(table, match.group(1), overwrite=overwrite)
    for match in _IDENT.finditer(blob):
        _remember(table, match.group(1).strip(), overwrite=overwrite)


def _data_dir(path: Path) -> Path | None:
    for parent in (path.parent, *path.parents):
        if (parent / "gdata.dat").is_file():
            return parent
        nested = parent / "Data" / "gdata.dat"
        if nested.is_file():
            return nested.parent
    return None


def name_dict_for(path: Path, file_bytes: bytes) -> dict[int, str]:
    """Map name hashes to strings found next to `path`.

    gdata.dat (quoted strings and identifiers) wins over cstrings in DLLs
    and in the file being dumped. Missing gdata is fine: unknown hashes stay
    as 0x........ .
    """
    data_dir = _data_dir(path)
    key = str(data_dir) if data_dir is not None else ""
    shared = _NAME_CACHE.get(key)
    if shared is None:
        shared = {}
        if data_dir is not None:
            gdata = data_dir / "gdata.dat"
            if gdata.is_file():
                _harvest(shared, gdata.read_bytes(), overwrite=True)
            game = data_dir.parent
            for pattern in ("*.dll", "*.ai"):
                for extra in game.glob(pattern):
                    try:
                        if extra.stat().st_size > 8_000_000:
                            continue
                        _harvest(shared, extra.read_bytes(), overwrite=False)
                    except OSError:
                        continue
        _NAME_CACHE[key] = shared
    names = dict(shared)
    _harvest(names, file_bytes, overwrite=False)
    return names


def load(path: Path) -> EvgFile:
    data = path.read_bytes()
    problems: list[str] = []
    if len(data) < 8 or data[:4] != b"EVG1":
        raise ValueError(f"{path} is not EVG1")
    index_bytes = struct.unpack_from("<I", data, 4)[0]
    if index_bytes % 8 or 8 + index_bytes > len(data):
        raise ValueError(f"{path}: bad index size {index_bytes}")
    count = index_bytes // 8
    index = [struct.unpack_from("<II", data, 8 + i * 8) for i in range(count)]
    body = data[8 + index_bytes :]
    if len(body) < 8 or body[:4] != b"MIKH":
        problems.append("data section does not start with MIKH")
    evg = EvgFile(path, data, index, body, problems)
    evg.names = name_dict_for(path, data)
    return evg


def _cstr(buf: bytes, off: int) -> str | None:
    if off < 0 or off >= len(buf):
        return None
    end = buf.find(b"\x00", off)
    if end < 0:
        return None
    return buf[off:end].decode("cp1251", "replace")


def _text(buf: bytes) -> str:
    raw = buf.split(b"\x00", 1)[0]
    return raw.decode("cp1251", "replace")


def _i32(buf: bytes) -> int | None:
    if len(buf) < 4:
        return None
    return struct.unpack_from("<i", buf, 0)[0]


def _f32(buf: bytes, off: int = 0) -> float | None:
    if len(buf) < off + 4:
        return None
    return struct.unpack_from("<f", buf, off)[0]


def _fmt_float(value: float) -> str:
    text = f"{value:.7g}"
    return text


def parse_block(evg: EvgFile, class_id: int, index: int, stack: tuple[int, ...]) -> Node:
    type_name = CLASS_NAME.get(class_id, f"0x{class_id:08X}")
    if class_id == NULL_CLASS or index == 0:
        # Index 0 is the empty directory slot. Class -1 is an array hole.
        if class_id == NULL_CLASS:
            return Node("null")
        if index == 0 and evg.index and evg.index[0][1] == 0:
            return Node("null", note="empty")
    if index in stack:
        return Node(type_name, block=index, note="cycle")
    payload = evg.payload(index)
    if payload is None:
        return Node(type_name, block=index, note="missing")
    child_stack = stack + (index,)
    if class_id not in CLASS_NAME:
        return Node(type_name, block=index, value=payload[:32].hex(), note="unknown class")
    if type_name == "int":
        value = _i32(payload)
        node = Node("int", block=index, value=value)
        if value is None:
            node.note = f"short ({len(payload)} bytes)"
        elif len(payload) != 4:
            node.note = f"size {len(payload)}"
        return node
    if type_name == "flt":
        value = _f32(payload)
        node = Node("flt", block=index, value=value)
        if value is None:
            node.note = f"short ({len(payload)} bytes)"
        elif len(payload) != 4:
            node.note = f"size {len(payload)}"
        return node
    if type_name == "v3f":
        coords = tuple(_f32(payload, i * 4) for i in range(3))
        node = Node("v3f", block=index, value=coords)
        if None in coords:
            node.note = f"short ({len(payload)} bytes)"
            node.value = payload.hex()
        elif len(payload) != 12:
            node.note = f"size {len(payload)}"
        return node
    if type_name == "txt":
        return Node("txt", block=index, value=_text(payload))
    if type_name == "bin":
        return Node("bin", block=index, value=payload)
    if type_name == "ref":
        # A path string ("\\Root\\Locations\\Instant Action"), framed like .txt.
        # It is not a class/index pair: those bytes are the path itself.
        return Node("ref", block=index, value=_text(payload))
    if type_name == "arr":
        return _parse_arr(evg, index, payload, child_stack)
    if type_name == "ctr":
        return _parse_ctr(evg, index, payload, child_stack)
    return Node(type_name, block=index, value=payload.hex())


def _parse_arr(evg: EvgFile, index: int, payload: bytes, stack: tuple[int, ...]) -> Node:
    node = Node("arr", block=index)
    if len(payload) < 4:
        node.note = "short"
        evg.problems.append(f"arr block {index} shorter than 4")
        return node
    count = struct.unpack_from("<I", payload, 0)[0]
    need = 4 + count * 8
    if count > 1_000_000 or need > len(payload):
        node.note = f"bad count {count}"
        evg.problems.append(f"arr block {index} count {count} size {len(payload)}")
        return node
    for i in range(count):
        class_id, child = struct.unpack_from("<II", payload, 4 + i * 8)
        element = parse_block(evg, class_id, child, stack)
        node.children.append(element)
    return node


def _parse_ctr(evg: EvgFile, index: int, payload: bytes, stack: tuple[int, ...]) -> Node:
    node = Node("ctr", block=index)
    if len(payload) < 4:
        node.note = "short"
        evg.problems.append(f"ctr block {index} shorter than 4")
        return node
    count = struct.unpack_from("<I", payload, 0)[0]
    need = 4 + count * 12
    if count > 1_000_000 or need > len(payload):
        node.note = f"bad count {count}"
        evg.problems.append(f"ctr block {index} count {count} size {len(payload)}")
        return node
    for i in range(count):
        class_id, child, name_off = struct.unpack_from("<III", payload, 4 + i * 12)
        name = _cstr(payload, name_off)
        if name is None:
            name = f"0x{name_off:08X}"
            evg.problems.append(f"ctr block {index} field {i} name offset {name_off}")
        element = parse_block(evg, class_id, child, stack)
        element.name = name
        if (
            element.type == "int"
            and name in HASH_FIELDS
            and isinstance(element.value, int)
            and element.value not in (0, -1)
        ):
            element.resolved = evg.names.get(element.value & 0xFFFFFFFF)
        node.children.append(element)
    return node


def parse(evg: EvgFile) -> Node:
    """Parse the tree rooted at the reference stored just after the MIKH header.

    That reference is block 1's payload: {u32 classId, u32 blockIndex}. Block 0
    is an empty directory slot; block 1 is the 8-byte MIKH header itself.
    """
    if len(evg.index) < 2 or evg.index[1][1] < 8:
        evg.problems.append("no root reference in block 1")
        return Node("null", note="no root")
    root_bytes = evg.payload(1)
    if root_bytes is None or len(root_bytes) < 8:
        evg.problems.append("root reference truncated")
        return Node("null", note="no root")
    class_id, index = struct.unpack_from("<II", root_bytes, 0)
    return parse_block(evg, class_id, index, ())


def _scalar_text(node: Node) -> str:
    if node.type == "txt":
        return json.dumps(node.value, ensure_ascii=False)
    if node.type == "int":
        if node.value is None:
            return "null"
        if node.resolved:
            return node.resolved
        if node.name in HASH_FIELDS and node.value not in (0, -1):
            return f"0x{node.value & 0xFFFFFFFF:08X}"
        return str(node.value)
    if node.type == "flt":
        return "null" if node.value is None else _fmt_float(node.value)
    if node.type == "v3f":
        if isinstance(node.value, tuple):
            return "(" + ", ".join("null" if v is None else _fmt_float(v) for v in node.value) + ")"
        return str(node.value)
    if node.type == "bin":
        raw: bytes = node.value  # type: ignore[assignment]
        preview = raw[:24].hex()
        if len(raw) > 24:
            preview += f"... ({len(raw)} bytes)"
        return preview
    if node.type == "ref" and node.value is not None:
        return str(node.value)
    return ""


def format_tree(node: Node, depth: int | None = None) -> str:
    lines: list[str] = []

    def walk(current: Node, indent: int, index_label: str | None) -> None:
        if depth is not None and indent > depth:
            return
        pad = "  " * indent
        head = current.type
        if current.name:
            head = f"{current.name}: {current.type}"
        elif index_label is not None:
            head = f"[{index_label}] {current.type}"
        extra = _scalar_text(current)
        if extra:
            head = f"{head} {extra}"
        if current.note:
            head = f"{head}  ({current.note})"
        if current.type == "arr":
            live = sum(1 for child in current.children if child.type != "null")
            holes = len(current.children) - live
            head = f"{head}  ({live}" + (f", {holes} empty" if holes else "") + ")"
        lines.append(pad + head)
        if depth is not None and indent >= depth:
            return
        if current.type == "arr":
            for i, child in enumerate(current.children):
                if child.type == "null":
                    continue
                walk(child, indent + 1, str(i))
        else:
            for child in current.children:
                walk(child, indent + 1, None)

    walk(node, 0, None)
    return "\n".join(lines)


def node_to_json(node: Node) -> object:
    if node.type == "null":
        return None
    out: dict[str, object] = {"type": node.type}
    if node.name is not None:
        out["name"] = node.name
    if node.block is not None:
        out["block"] = node.block
    if node.note:
        out["note"] = node.note
    if node.type == "bin":
        raw = node.value if isinstance(node.value, bytes) else b""
        out["size"] = len(raw)
        out["hex"] = raw.hex()
    elif node.type in {"int", "flt", "txt", "ref"} and node.value is not None and not node.children:
        out["value"] = node.value
        if node.resolved:
            out["text"] = node.resolved
        elif (
            node.type == "int"
            and node.name in HASH_FIELDS
            and isinstance(node.value, int)
            and node.value not in (0, -1)
        ):
            out["text"] = f"0x{node.value & 0xFFFFFFFF:08X}"
    elif node.type == "v3f":
        out["value"] = list(node.value) if isinstance(node.value, tuple) else node.value
    if node.type == "arr":
        out["items"] = [node_to_json(child) for child in node.children]
    elif node.children:
        out["children"] = [node_to_json(child) for child in node.children]
    return out


def dump_file(path: Path, as_json: bool, depth: int | None) -> int:
    evg = load(path)
    root = parse(evg)
    if as_json:
        document = {
            "file": str(path),
            "blocks": evg.block_count,
            "problems": evg.problems,
            "root": node_to_json(root),
        }
        json.dump(document, sys.stdout, ensure_ascii=False, indent=2)
        sys.stdout.write("\n")
    else:
        sys.stdout.write(format_tree(root, depth))
        sys.stdout.write("\n")
        if evg.problems:
            sys.stdout.write(f"\n# {len(evg.problems)} problem(s)\n")
            for problem in evg.problems[:20]:
                sys.stdout.write(f"# {problem}\n")
    return 1 if evg.problems else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Dump an EVG1 variable tree.")
    parser.add_argument("path", type=Path, help="EVG1 file (.gsl, .gsd, .ros, .dat, ...)")
    parser.add_argument("--json", action="store_true", help="write a JSON document")
    parser.add_argument(
        "--depth",
        type=int,
        default=None,
        help="limit the text dump to this many nested levels (0 = root only)",
    )
    args = parser.parse_args(argv)
    try:
        return dump_file(args.path, args.json, args.depth)
    except ValueError as exc:
        sys.stderr.write(f"{exc}\n")
        return 2


if __name__ == "__main__":
    sys.exit(main())
