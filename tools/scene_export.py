#!/usr/bin/env python3
"""Place one campaign event onto a terrain scene.

Reads the event's groups from Wind Warriors.gsd (EVG1, see docs/specs/evg.md)
and writes assets/legacy/scenes/<terrain>.scene.json. Statics and hangars are
always included. Ground units (tanks and the like) are included unless
--no-units is passed. Aircraft stay out: their Org.y is an altitude, and the
sandbox already lines the exported craft up on the field.

    python tools/scene_export.py --event O1-1

Org is D3D metres. x/z become engine (x, -z). y on a static or a ground unit
is an offset above the heightfield (on_ground), not an absolute altitude.
Angle is a yaw in degrees about Y; the scene loader treats heading 0 as
facing -Z and turns right for a positive heading, which lines the training-
centre walls up with the rows they were authored in.

Windmills are Static("WindMill") on the location, not in the event, so they
are added when they fall inside the mission's footprint. Road polylines from
the same location are written to <terrain>.roads.json (Type="Road" only).
Bridges (Type="Bridge") go into the scene file as "bridges": the entrance and
section meshes are exported and the app tiles them along the span
(docs/specs/roads.md).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import legacy_export
from evg_dump import Node, load, parse

# Map dressing that lives on the location, not in the mission event. The
# training-centre turbines are Static("WindMill"), group "H WindMill (TCD12)".
LOCATION_DECOR = {"WindMill", "WindMill Velian"}
# How far past the mission's own objects a location road or turbine still counts.
NEAR_MARGIN = 1500.0

STATIC_AI = {"StdStatic", "StdStaticHangar", "StdStaticSfg"}
GROUND_AI = {"StdTank", "StdHTGR", "StdSingleCargo"}
ENTITY_OF_AI = {
    "StdStatic": "Static",
    "StdStaticHangar": "Static",
    "StdStaticSfg": "Static",
    "StdTank": "Vehicle",
    "StdHTGR": "Vehicle",
    "StdSingleCargo": "Vehicle",
}


def _child(node: Node, name: str) -> Node | None:
    for item in node.children:
        if item.name == name:
            return item
    return None


def _text(node: Node | None) -> str:
    if node is None:
        return ""
    if node.resolved:
        return node.resolved
    if node.type in {"txt", "ref"} and isinstance(node.value, str):
        return node.value
    return ""


def _float(node: Node | None) -> float | None:
    if node is None or node.type != "flt" or not isinstance(node.value, (int, float)):
        return None
    return float(node.value)


def _org(unit: Node) -> tuple[float, float, float] | None:
    org = _child(unit, "Org")
    if org is None or org.type != "v3f" or not isinstance(org.value, tuple) or len(org.value) != 3:
        return None
    if any(v is None for v in org.value):
        return None
    return float(org.value[0]), float(org.value[1]), float(org.value[2])


def _event(root: Node, event_id: str) -> Node:
    events = _child(root, "Events")
    if events is None:
        raise SystemExit("gsd has no Events")
    event = _child(events, event_id)
    if event is None:
        known = ", ".join(c.name or "?" for c in events.children[:12])
        raise SystemExit(f"event {event_id!r} not found (first ids: {known})")
    return event


def _collect(event: Node, want_ai: set[str]) -> list[dict]:
    groups = _child(event, "Groups")
    if groups is None:
        return []
    out: list[dict] = []
    for group in groups.children:
        units = _child(group, "Units")
        if units is None:
            continue
        for unit in units.children:
            if unit.type == "null":
                continue
            ai = _text(_child(unit, "Ai"))
            if ai not in want_ai:
                continue
            org = _org(unit)
            name = _text(_child(unit, "Name"))
            if org is None or not name:
                print(f"  skip {group.name}: {ai} without name/org", file=sys.stderr)
                continue
            angle = _float(_child(unit, "Angle")) or 0.0
            x, y, z = org
            out.append({
                "name": name,
                "ai": ai,
                "model": "",
                "position": [round(x, 2), round(y, 2), round(-z, 2)],
                "heading_deg": round(angle, 2),
                "on_ground": True,
                "group": group.name or "",
            })
    return out


def _mesh_file(sources: legacy_export.Sources, name: str, entity: str) -> str | None:
    try:
        mesh, _root = legacy_export.parse_craft_hull(sources.gdata, name, entity, window=100000)
    except (KeyError, ValueError) as exc:
        print(f"  skip {entity} {name!r}: {exc}", file=sys.stderr)
        return None
    return mesh


def _export_model(data_dir: Path, out_dir: Path, name: str, entity: str,
                  sources: legacy_export.Sources, objects2, textures: set[str],
                  extras: list) -> bool:
    if entity == "Static":
        return legacy_export.export_static(
            data_dir, name, out_dir, lod=0, damage=0, sources=sources,
            exported_textures=textures, objects2=objects2, extra_textures=extras)
    return legacy_export.export_craft(
        data_dir, name, out_dir, lod=0, damage=0, sources=sources,
        exported_textures=textures, entity=entity, json_key="vehicle",
        pos_limit=4000.0, bbox_limit=4000.0, window=100000,
        objects2=objects2, extra_textures=extras)


def _bounds(placed: list[dict], margin: float) -> tuple[float, float, float, float] | None:
    if not placed:
        return None
    xs = [item["position"][0] for item in placed]
    zs = [item["position"][2] for item in placed]
    return min(xs) - margin, max(xs) + margin, min(zs) - margin, max(zs) + margin


def _inside(x: float, z: float, box: tuple[float, float, float, float]) -> bool:
    minx, maxx, minz, maxz = box
    return minx <= x <= maxx and minz <= z <= maxz


def _location_node(root: Node, terrain: str) -> Node | None:
    locations = _child(root, "Locations")
    if locations is None:
        return None
    for item in locations.children:
        if item.name and item.name.casefold() == terrain.casefold():
            return item
    return None


def _windmills(location: Node, box: tuple[float, float, float, float]) -> list[dict]:
    groups = _child(location, "Groups")
    if groups is None:
        return []
    out: list[dict] = []
    for group in groups.children:
        units = _child(group, "Units")
        if units is None:
            continue
        for unit in units.children:
            if unit.type == "null":
                continue
            if _text(_child(unit, "Ai")) not in STATIC_AI:
                continue
            name = _text(_child(unit, "Name"))
            if name not in LOCATION_DECOR:
                continue
            org = _org(unit)
            if org is None:
                continue
            x, y, z = org
            if not _inside(x, -z, box):
                continue
            angle = _float(_child(unit, "Angle")) or 0.0
            out.append({
                "name": name,
                "ai": "StdStatic",
                "model": "",
                "position": [round(x, 2), round(y, 2), round(-z, 2)],
                "heading_deg": round(angle, 2),
                "on_ground": True,
                "group": group.name or "",
            })
    return out


def _road_catalog(gdata: bytes) -> dict[str, tuple[float, str]]:
    """Name -> (width metres, section texture) for Type="Road" only."""
    text = gdata.decode("cp1251", "replace")
    catalog: dict[str, tuple[float, str]] = {}
    # Some blocks are written `Road ("Name")` with a space; the name itself has none.
    for match in re.finditer(r'Road\s*\("([^"]+)"\)\s*\{', text):
        window = text[match.end():match.end() + 900]
        end = window.find("\n}")
        block = window[:end] if end >= 0 else window
        kind = re.search(r'Type\s*=\s*"([^"]+)"', block)
        width = re.search(r"Width\s*=\s*([0-9.]+)", block)
        texture = re.search(r'SectionTexture\s*=\s*"([^"]+)"', block)
        if not kind or kind.group(1) != "Road" or not width or not texture:
            continue
        metres = float(width.group(1))
        if metres > 200.0:
            continue
        catalog[match.group(1)] = (metres, texture.group(1))
    return catalog


def _bridge_catalog(gdata: bytes) -> dict[str, tuple[str, str]]:
    """Name -> (entrance mesh, section mesh) for Type="Bridge"."""
    text = gdata.decode("cp1251", "replace")
    catalog: dict[str, tuple[str, str]] = {}
    for match in re.finditer(r'Road\s*\("([^"]+)"\)\s*\{', text):
        window = text[match.end():match.end() + 900]
        end = window.find("\n}")
        block = window[:end] if end >= 0 else window
        kind = re.search(r'Type\s*=\s*"([^"]+)"', block)
        entrance = re.search(r'EntranceObject\s*=\s*"([^"]+)"', block)
        section = re.search(r'SectionObject\s*=\s*"([^"]+)"', block)
        if not kind or kind.group(1) != "Bridge" or not entrance or not section:
            continue
        catalog[match.group(1)] = (entrance.group(1), section.group(1))
    return catalog


def _polylines_near(location: Node, box: tuple[float, float, float, float],
                    names: set[str]) -> list[tuple[str, list[list[float]]]]:
    """(name, engine points) of every Roads entry with a known name that touches the box."""
    roads = _child(location, "Roads")
    if roads is None or roads.type != "arr":
        return []
    out: list[tuple[str, list[list[float]]]] = []
    for road in roads.children:
        if road.type == "null":
            continue
        name = _text(_child(road, "Name"))
        if name not in names:
            continue
        points = _child(road, "Points")
        if points is None or points.type != "arr":
            continue
        engine: list[list[float]] = []
        hit = False
        for point in points.children:
            if point.type != "v3f" or not isinstance(point.value, tuple):
                continue
            if any(v is None for v in point.value):
                continue
            x, y, z = (float(v) for v in point.value)
            ex, ez = x, -z
            engine.append([round(ex, 2), round(y, 2), round(ez, 2)])
            if _inside(ex, ez, box):
                hit = True
        if hit and len(engine) >= 2:
            out.append((name, engine))
    return out


def _roads_near(location: Node, box: tuple[float, float, float, float],
                catalog: dict[str, tuple[float, str]]) -> list[dict]:
    out: list[dict] = []
    for name, engine in _polylines_near(location, box, set(catalog)):
        width, texture = catalog[name]
        out.append({
            "name": name,
            "width": width,
            "texture": texture,
            "points": engine,
        })
    return out


def _deck_height(sources: legacy_export.Sources, mesh: str) -> float:
    """Engine y of the road surface: the most common vertex height of the mesh.

    Verified on riverbridge200m_be/_part, highwaybridge200m_*, railroadbridge600m_*,
    seabridge600m_*: 17.9..18.0 m, the deck is by far the largest flat face.
    """
    pm = legacy_export.parse_lod(sources.meshes, sources.textures, sources.materials,
                                 legacy_export._record_name(mesh, legacy_export.HULL_PART, 0, 0))
    counts: dict[float, int] = {}
    for v in pm.vertices:
        key = round(v[1], 1)
        counts[key] = counts.get(key, 0) + 1
    return max(counts, key=counts.get)


def _bridges_near(location: Node, box: tuple[float, float, float, float],
                  catalog: dict[str, tuple[str, str]], sources: legacy_export.Sources) -> list[dict]:
    out: list[dict] = []
    decks: dict[str, float] = {}
    for name, engine in _polylines_near(location, box, set(catalog)):
        entrance, section = catalog[name]
        if section not in decks:
            decks[section] = _deck_height(sources, section)
        out.append({
            "name": name,
            "entrance": entrance,
            "section": section,
            "deck_y": decks[section],
            "points": engine,
        })
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--event", default="O1-1", help="event id in Wind Warriors.gsd (default O1-1)")
    ap.add_argument("--data", type=Path, default=Path("legacy/Echelon Wind Warriors/Data"))
    ap.add_argument("--gsd", type=Path, default=None, help="defaults to <data>/Wind Warriors.gsd")
    ap.add_argument("--out", type=Path, default=Path("assets/legacy"))
    ap.add_argument("--no-units", action="store_true", help="statics and hangars only")
    args = ap.parse_args()

    gsd = args.gsd or (args.data / "Wind Warriors.gsd")
    evg = load(gsd)
    root = parse(evg)
    event = _event(root, args.event)
    location = _text(_child(event, "Location"))
    terrain = "Arctic" if location.endswith("Arctic") else "Continent"
    if location.endswith("Races"):
        raise SystemExit(f"{args.event} is on {location}; this exporter places Continent/Arctic only")

    want = set(STATIC_AI)
    if not args.no_units:
        want |= GROUND_AI
    placed = _collect(event, want)
    box = _bounds(placed, NEAR_MARGIN)
    location_node = _location_node(root, terrain)
    near_roads: list[dict] = []
    if box is not None and location_node is not None:
        mills = _windmills(location_node, box)
        print(f"windmills inside the mission footprint: {len(mills)}")
        placed.extend(mills)
    print(f"{args.event}: {len(placed)} placements on {terrain}")

    sources = legacy_export.load_sources(args.data)
    near_bridges: list[dict] = []
    if box is not None and location_node is not None:
        near_roads = _roads_near(location_node, box, _road_catalog(sources.gdata))
        near_bridges = _bridges_near(location_node, box, _bridge_catalog(sources.gdata), sources)
    print(f"roads crossing the footprint: {len(near_roads)}, bridges: {len(near_bridges)}")
    objects2 = legacy_export.Container.load(args.data / "objects2.dat")
    atex = legacy_export._load_optional(args.data / "Graphics" / "atextures.dat")
    extras = [atex] if atex is not None else []
    textures: set[str] = set()

    by_name: dict[tuple[str, str], str | None] = {}
    for item in placed:
        entity = ENTITY_OF_AI[item["ai"]]
        key = (entity, item["name"])
        if key not in by_name:
            by_name[key] = _mesh_file(sources, item["name"], entity)
        item["model"] = by_name[key] or ""

    models_dir = args.out / "models"
    exported: set[str] = set()
    for (entity, name), mesh in by_name.items():
        if not mesh:
            continue
        if mesh in exported:
            continue
        json_path = models_dir / f"{mesh}_im0.model.json"
        if json_path.is_file():
            print(f"== {name} ({mesh}) already exported")
            exported.add(mesh)
            continue
        if _export_model(args.data, args.out, name, entity, sources, objects2, textures, extras):
            exported.add(mesh)
        else:
            print(f"  export failed: {entity} {name}", file=sys.stderr)

    # Bridge meshes have no Static() block; export_static falls back to the mesh file.
    bridge_meshes = sorted({m for b in near_bridges for m in (b["entrance"], b["section"])})
    for mesh in bridge_meshes:
        if (models_dir / f"{mesh}_im0.model.json").is_file():
            print(f"== {mesh} already exported")
            exported.add(mesh)
            continue
        if legacy_export.export_static(args.data, mesh, args.out, lod=0, damage=0, sources=sources,
                                       exported_textures=textures, objects2=objects2,
                                       extra_textures=extras):
            exported.add(mesh)
        else:
            print(f"  export failed: bridge mesh {mesh}", file=sys.stderr)
    bridges = [b for b in near_bridges if b["entrance"] in exported and b["section"] in exported]

    objects = []
    missing_models = 0
    for item in placed:
        if not item["model"] or item["model"] not in exported:
            missing_models += 1
            continue
        objects.append({
            "model": item["model"],
            "position": item["position"],
            "heading_deg": item["heading_deg"],
            "on_ground": True,
        })

    scene_dir = args.out / "scenes"
    scene_dir.mkdir(parents=True, exist_ok=True)
    scene_path = scene_dir / f"{terrain}.scene.json"
    document = {
        "terrain": terrain,
        "mission": args.event,
        "objects": objects,
        "bridges": bridges,
    }
    scene_path.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(objects)} objects and {len(bridges)} bridges to {scene_path}"
          + (f" ({missing_models} skipped, no model)" if missing_models else ""))

    tex_dir = args.out / "textures"
    tex_dir.mkdir(parents=True, exist_ok=True)
    for texture in sorted({road["texture"] for road in near_roads}):
        if (tex_dir / f"{texture}.png").is_file():
            continue
        legacy_export.export_texture(sources.textures, texture, tex_dir, extras)
    roads_path = scene_dir / f"{terrain}.roads.json"
    roads_path.write_text(json.dumps({
        "terrain": terrain,
        "mission": args.event,
        "roads": near_roads,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(near_roads)} roads to {roads_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
