#!/usr/bin/env python3
"""Export a terrain (height field + ground texture layers) from the original data.

Reads Data/Scenes/<name>.hd/.sq/.vb and Data/Graphics/rdata.dat + textures.dat,
writes:

    <out>/terrain/<name>.eterr          heights, diagonal flags, texture cells
    <out>/terrain/<name>.terrain.json   layer slot -> texture, detail/water textures
    <out>/textures/<tex>.png            ground textures (shared with the models)

Original layout (see docs/specs/terrain.md):
    .hd  u32 chunksX, chunksY, ...; one chunk = 128 x 128 samples
    .sq  {i16 height, u16 flags} per sample, tile-major: tiles of 32 x 32
         samples, tiles row-major across the map. height * 0.2 = metres,
         sample spacing 64 m. flags bit 15: the quad (i, j)-(i+1, j+1) is split
         along that diagonal, otherwise along (i+1, j)-(i, j+1). Bits 0-2 hold a
         0..7 class (probably the surface type), bit 11 is unknown; neither is
         exported yet.
    .vb  16-byte record per 4 x 4 samples, tile-major in tiles of 16 x 16
         records: u16 3, i16 water level * 0.2 (-2500 = none), i16, u16 0,
         u8 layer A, u8 layer B (0xFF = none), u16 101, u32 mask. Bit
         (vy * 5 + vx) of the mask selects layer B for vertex (vx, vy) of the
         5 x 5 corners of the cell, otherwise layer A.
    rdata.dat "default#terrmtl": detail + water headers, then 32 layer slots of
         {u32 textureHash, u32 materialHash, u32 -1, u32 0} from offset 52.

.eterr v2 (little endian) is a zlib wrapper around the v1 layout:
    'ETER', u32 version=2, u32 uncompressedSize, zlib(v1 file)
v1:
    'ETER', u32 version=1, u32 width, u32 height (samples), u32 cellsX, u32 cellsY,
    f32 sampleSpacing, f32 heightScale
    i16 height[height][width]            row j = original row (D3D +Z)
    u8  flags[height][width]             high byte of the .sq flags (bit 7 = diagonal, bit 3 = ?)
    u8  cell[cellsY][cellsX][16]         raw .vb records, row-major
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from array import array
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from legacy_export import Container, decode_dxt, export_texture  # noqa: E402

ETERR_VERSION = 1
ETERR_ZLIB_VERSION = 2
SAMPLE_SPACING = 64.0
HEIGHT_SCALE = 0.2
SQ_TILE = 32
VB_TILE = 16
LAYER_SLOTS = 32
LAYER_TABLE_OFFSET = 52


def reorder_tiles(src, tile: int, tiles_x: int, tiles_y: int, elem: int):
    """Tile-major (tile x tile blocks, row-major tiles) -> row-major rows.

    `src` is an indexable sequence of elements; `elem` elements form one item
    (used for 16-byte .vb records). Returns the same type as `src`.
    """
    row_items = tile * elem
    tile_items = tile * tile * elem
    out = src[:0]
    parts = []
    for ty in range(tiles_y):
        for r in range(tile):
            for tx in range(tiles_x):
                start = (ty * tiles_x + tx) * tile_items + r * row_items
                parts.append(src[start:start + row_items])
    if isinstance(out, array):
        for p in parts:
            out.extend(p)
        return out
    return type(src)().join(parts)


def layer_textures(rdata: Container, textures: Container) -> tuple[list[str | None], str | None, str | None]:
    rec = rdata.record("default#terrmtl")
    detail_hash = struct.unpack_from("<I", rec, 8)[0]
    water_hash = struct.unpack_from("<I", rec, 52)[0]
    layers: list[str | None] = []
    for i in range(LAYER_SLOTS):
        tex_hash = struct.unpack_from("<I", rec, LAYER_TABLE_OFFSET + i * 16)[0]
        layers.append(textures.by_hash.get(tex_hash))
    return layers, textures.by_hash.get(detail_hash), textures.by_hash.get(water_hash)


def mean_srgb(textures: Container, name: str) -> list[float]:
    rec = textures.record(name)
    width, height = struct.unpack_from("<II", rec, 16)
    rows = decode_dxt(rec[56:], width, height, rec[48:52])
    acc = [0, 0, 0]
    for row in rows:
        for c in range(3):
            acc[c] += sum(row[c::4])
    n = width * height * 255.0
    return [round(a / n, 4) for a in acc]


def export_terrain(data_dir: Path, name: str, out_dir: Path) -> None:
    scenes = data_dir / "Scenes"
    graphics = data_dir / "Graphics"
    chunks_x, chunks_y = struct.unpack_from("<II", (scenes / f"{name}.hd").read_bytes())
    width, height = chunks_x * 128, chunks_y * 128
    cells_x, cells_y = width // 4, height // 4

    sq = (scenes / f"{name}.sq").read_bytes()
    if len(sq) != width * height * 4:
        raise ValueError(f"{name}.sq: {len(sq)} bytes, expected {width * height * 4}")
    vb = (scenes / f"{name}.vb").read_bytes()
    if len(vb) != cells_x * cells_y * 16:
        raise ValueError(f"{name}.vb: {len(vb)} bytes, expected {cells_x * cells_y * 16}")

    words = array("h")
    words.frombytes(sq)
    if sys.byteorder != "little":
        words.byteswap()
    heights = reorder_tiles(words[0::2], SQ_TILE, width // SQ_TILE, height // SQ_TILE, 1)
    flags = reorder_tiles(sq[3::4], SQ_TILE, width // SQ_TILE, height // SQ_TILE, 1)
    cells = reorder_tiles(vb, VB_TILE, cells_x // VB_TILE, cells_y // VB_TILE, 16)
    print(f"{name}: {width} x {height} samples, heights {min(heights) * HEIGHT_SCALE:.0f}.."
          f"{max(heights) * HEIGHT_SCALE:.0f} m")

    terrain_dir = out_dir / "terrain"
    tex_dir = out_dir / "textures"
    terrain_dir.mkdir(parents=True, exist_ok=True)
    tex_dir.mkdir(parents=True, exist_ok=True)

    if sys.byteorder != "little":
        heights.byteswap()
    plain = bytearray()
    plain += b"ETER"
    plain += struct.pack("<5I2f", ETERR_VERSION, width, height, cells_x, cells_y, SAMPLE_SPACING, HEIGHT_SCALE)
    plain += heights.tobytes()
    plain += flags
    plain += cells
    compressed = zlib.compress(plain, 9)
    out = terrain_dir / f"{name}.eterr"
    with out.open("wb") as f:
        f.write(b"ETER")
        f.write(struct.pack("<II", ETERR_ZLIB_VERSION, len(plain)))
        f.write(compressed)
    print(f"  -> {out} ({out.stat().st_size / 1e6:.1f} MB, plain {len(plain) / 1e6:.1f} MB)")

    textures = Container.load(graphics / "textures.dat")
    rdata = Container.load(graphics / "rdata.dat")
    layers, detail, water = layer_textures(rdata, textures)
    for tex in {t for t in layers + [detail, water] if t}:
        export_texture(textures, tex, tex_dir)

    desc = {
        "name": name,
        "heights": out.name,
        "sample_spacing": SAMPLE_SPACING,
        "height_scale": HEIGHT_SCALE,
        "layers": layers,
        "detail": detail,
        "detail_mean_srgb": mean_srgb(textures, detail) if detail else None,
        "water": water,
    }
    desc_path = terrain_dir / f"{name}.terrain.json"
    desc_path.write_text(json.dumps(desc, indent=2), encoding="utf-8")
    print(f"  -> {desc_path}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--data", type=Path, default=Path("legacy/Echelon/Data"),
                    help="original Data directory (default: legacy/Echelon/Data)")
    ap.add_argument("--out", type=Path, default=Path("assets/legacy"),
                    help="output directory (default: assets/legacy, git-ignored)")
    ap.add_argument("--terrain", default="Continent", help="scene name in Data/Scenes (default: Continent)")
    args = ap.parse_args()
    export_terrain(args.data, args.terrain, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
