#!/usr/bin/env python3
"""Survey original Storm engine binaries without disassembling them.

Reads PE32 exports, imports, the CodeView PDB path and MSVC RTTI class names.
Also collects assert/log strings that still name a .cpp file or a Class::method.
MSVC export names are undecorated through dbghelp when it is available.

    python tools/pe_survey.py legacy/Echelon "legacy/Echelon Wind Warriors"
    python tools/pe_survey.py --json tools/temp/pe.json legacy/Echelon/StormGame.dll

A directory is walked for .exe / .dll / .ai. The duplicate tree P1 K6x/ and
bundled third-party DLLs (Python, GDI+, GameSpy, the CRT) are skipped.
A path to one file is surveyed even if it would have been skipped in a walk.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import re
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path

SKIP_DIR = "P1 K6x"
SKIP_NAMES = {
    "gdiplus.dll",
    "msvcp60.dll",
    "python22.dll",
    "pyudb.dll",
    "mdlgft.dll",
    "gamespysupport.dll",
}
SYSTEM_DLLS = {
    "kernel32.dll",
    "user32.dll",
    "gdi32.dll",
    "advapi32.dll",
    "shell32.dll",
    "ole32.dll",
    "ws2_32.dll",
    "msvcrt.dll",
    "msvcp60.dll",
    "mfc42.dll",
    "comctl32.dll",
    "comdlg32.dll",
    "ddraw.dll",
    "dsound.dll",
    "dinput.dll",
    "dinput8.dll",
    "avifil32.dll",
    "python22.dll",
    "gdiplus.dll",
}

PRINTABLE = re.compile(rb"[\x20-\x7e]{6,220}")
SOURCE_NAME = re.compile(r"([A-Za-z0-9_]+\.(?:cpp|h|inl))\b", re.IGNORECASE)
LOG_NAME = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_~][A-Za-z0-9_]*)+)")

UNDNAME_NO_ACCESS_SPECIFIERS = 0x0080
UNDNAME_32_BIT_DECODE = 0x0800
UNDNAME_NAME_ONLY = 0x1000
UNDNAME_NO_MEMBER_TYPE = 0x0200
UNDNAME_FLAGS = (
    UNDNAME_NAME_ONLY
    | UNDNAME_32_BIT_DECODE
    | UNDNAME_NO_ACCESS_SPECIFIERS
    | UNDNAME_NO_MEMBER_TYPE
)


def u16(b: bytes, o: int) -> int:
    return struct.unpack_from("<H", b, o)[0]


def u32(b: bytes, o: int) -> int:
    return struct.unpack_from("<I", b, o)[0]


def cstr(b: bytes, o: int, limit: int = 512) -> str:
    end = b.find(b"\x00", o, o + limit)
    if end < 0:
        end = min(len(b), o + limit)
    return b[o:end].decode("latin1", errors="replace")


class Undecorator:
    def __init__(self) -> None:
        self._fn = None
        try:
            dbghelp = ctypes.WinDLL("dbghelp")
            self._fn = dbghelp.UnDecorateSymbolName
            self._fn.argtypes = [
                ctypes.c_char_p,
                ctypes.c_char_p,
                ctypes.c_uint,
                ctypes.c_uint,
            ]
            self._fn.restype = ctypes.c_uint
        except OSError:
            self._fn = None

    def __call__(self, name: str) -> str:
        if self._fn is None or not (name.startswith("?") or name.startswith("_")):
            return name
        buf = ctypes.create_string_buffer(1024)
        n = self._fn(name.encode("ascii", errors="replace"), buf, 1024, UNDNAME_FLAGS)
        if n <= 0:
            return name
        return buf.value.decode("ascii", errors="replace")


def rtti_class(mangled: str) -> str:
    # .?AVClass@ns@@  ->  ns::Class
    body = mangled[4:]
    if body.endswith("@@"):
        body = body[:-2]
    parts = [p for p in body.split("@") if p and not p.startswith("?")]
    if not parts:
        return mangled
    parts.reverse()
    return "::".join(parts)


class Image:
    def __init__(self, path: Path, data: bytes) -> None:
        self.path = path
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("not MZ")
        self.e_lfanew = u32(data, 0x3C)
        if data[self.e_lfanew : self.e_lfanew + 4] != b"PE\x00\x00":
            raise ValueError("not PE")
        coff = self.e_lfanew + 4
        self.nsections = u16(data, coff + 2)
        self.timestamp = u32(data, coff + 4)
        self.nsymbols = u32(data, coff + 12)
        self.characteristics = u16(data, coff + 18)
        opt = coff + 20
        self.magic = u16(data, opt)
        if self.magic != 0x10B:
            raise ValueError(f"not PE32 (magic {self.magic:#x})")
        self.linker = (data[opt + 2], data[opt + 3])
        self.opt_size = u16(data, coff + 16)
        dd = opt + 96
        self.dirs = [struct.unpack_from("<II", data, dd + 8 * i) for i in range(16)]
        sec = opt + self.opt_size
        self.sections = []
        for i in range(self.nsections):
            o = sec + 40 * i
            name = data[o : o + 8].split(b"\x00", 1)[0].decode("latin1")
            self.sections.append(
                {
                    "name": name,
                    "vsize": u32(data, o + 8),
                    "va": u32(data, o + 12),
                    "raw_size": u32(data, o + 16),
                    "raw": u32(data, o + 20),
                }
            )

    def rva_to_off(self, rva: int) -> int | None:
        for s in self.sections:
            span = max(s["vsize"], s["raw_size"])
            if s["va"] <= rva < s["va"] + span:
                return s["raw"] + (rva - s["va"])
        return None

    def read_cstr_rva(self, rva: int) -> str:
        off = self.rva_to_off(rva)
        if off is None:
            return ""
        return cstr(self.data, off)

    def exports(self) -> list[str]:
        rva, size = self.dirs[0]
        if not rva or not size:
            return []
        off = self.rva_to_off(rva)
        if off is None:
            return []
        nnames = u32(self.data, off + 24)
        names_rva = u32(self.data, off + 32)
        names_off = self.rva_to_off(names_rva)
        if names_off is None:
            return []
        out = []
        for i in range(nnames):
            nrva = u32(self.data, names_off + 4 * i)
            out.append(self.read_cstr_rva(nrva))
        return out

    def imports(self) -> dict[str, list[str]]:
        rva, size = self.dirs[1]
        result: dict[str, list[str]] = {}
        if not rva or not size:
            return result
        off = self.rva_to_off(rva)
        if off is None:
            return result
        i = 0
        while i < 256:
            base = off + 20 * i
            if base + 20 > len(self.data):
                break
            oft = u32(self.data, base)
            name_rva = u32(self.data, base + 12)
            ft = u32(self.data, base + 16)
            if oft == 0 and name_rva == 0 and ft == 0:
                break
            dll = self.read_cstr_rva(name_rva)
            thunk_rva = oft or ft
            thunk_off = self.rva_to_off(thunk_rva)
            names: list[str] = []
            if thunk_off is not None:
                j = 0
                while j < 8000:
                    val = u32(self.data, thunk_off + 4 * j)
                    if val == 0:
                        break
                    if val & 0x80000000:
                        names.append(f"#{val & 0xFFFF}")
                    else:
                        hint_off = self.rva_to_off(val)
                        if hint_off is not None:
                            names.append(cstr(self.data, hint_off + 2))
                    j += 1
            result[dll] = names
            i += 1
        return result

    def pdb(self) -> str | None:
        rva, size = self.dirs[6]
        if not rva or not size:
            return None
        off = self.rva_to_off(rva)
        if off is None:
            return None
        n = size // 28
        for i in range(n):
            typ = u32(self.data, off + 28 * i + 12)
            raw = u32(self.data, off + 28 * i + 24)
            if typ != 2:  # IMAGE_DEBUG_TYPE_CODEVIEW
                continue
            if raw + 4 > len(self.data):
                continue
            sig = self.data[raw : raw + 4]
            if sig == b"RSDS":
                return cstr(self.data, raw + 24)
            if sig == b"NB10":
                return cstr(self.data, raw + 16)
        return None

    def rtti_names(self) -> list[str]:
        found: set[str] = set()
        blob = self.data
        for marker in (b".?AV", b".?AU"):
            start = 0
            while True:
                i = blob.find(marker, start)
                if i < 0:
                    break
                s = cstr(blob, i, 240)
                if "@@" in s and all(32 <= ord(c) < 127 for c in s):
                    found.add(s)
                start = i + 4
        return sorted(found)


def interesting_strings(data: bytes) -> tuple[list[str], list[str], list[str]]:
    """Return (source basenames, Class::method logs, version banners)."""
    sources: set[str] = set()
    logs: set[str] = set()
    banners: set[str] = set()
    for match in PRINTABLE.finditer(data):
        text = match.group().decode("ascii")
        low = text.lower()
        if ("echelon" in low or "sources" in low or "include" in low) and (
            ".cpp" in low or ".h" in low or ".inl" in low
        ):
            sources.update(SOURCE_NAME.findall(text))
        if "::" in text and text[0].isalpha():
            for name in LOG_NAME.findall(text):
                head = name.split("::", 1)[0]
                if head in {"std", "boost", "py"} or head.startswith("_"):
                    continue
                if "eee" in name:
                    continue
                logs.add(name)
        if " build " in text and len(text) < 140 and "eee" not in text:
            banners.add(text.lstrip("?"))
    return sorted(sources), sorted(logs), sorted(banners)


def survey_file(path: Path, undec: Undecorator) -> dict:
    data = path.read_bytes()
    img = Image(path, data)
    exports = [{"raw": raw, "name": undec(raw)} for raw in img.exports()]
    imports = {
        dll: [undec(name) for name in names] for dll, names in img.imports().items()
    }
    sources, logs, banners = interesting_strings(data)
    return {
        "file": path.name,
        "path": str(path),
        "size": len(data),
        "timestamp": img.timestamp,
        "linker": f"{img.linker[0]}.{img.linker[1]:02d}",
        "coff_symbols": img.nsymbols,
        "sections": [s["name"] for s in img.sections],
        "pdb": img.pdb(),
        "exports": exports,
        "imports": imports,
        "rtti": [rtti_class(n) for n in img.rtti_names()],
        "sources": sources,
        "logs": logs,
        "banners": banners,
    }


def iter_files(root: Path):
    if root.is_file():
        yield root
        return
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() not in {".exe", ".dll", ".ai"}:
            continue
        if SKIP_DIR in path.parts:
            continue
        if path.name.lower() in SKIP_NAMES:
            continue
        if path.read_bytes()[:2] != b"MZ":
            continue
        yield path


def format_report(modules: list[dict]) -> str:
    lines: list[str] = []
    for m in modules:
        if "error" in m:
            lines.append(f"\n== {m.get('path', '?')}  ERROR {m['error']}")
            continue
        when = datetime.fromtimestamp(m["timestamp"], timezone.utc).strftime("%Y-%m-%d")
        lines.append(
            f"\n== {m['file']}  {m['size']}  linker {m['linker']}  {when}"
            f"  coff_symbols={m['coff_symbols']}"
        )
        lines.append(f"   path {m['path']}")
        if m["pdb"]:
            lines.append(f"   pdb {m['pdb']}")
        lines.append(f"   sections {' '.join(m['sections'])}")
        if m["banners"]:
            for banner in m["banners"]:
                lines.append(f"   banner {banner}")
        own = [e["name"] for e in m["exports"]]
        lines.append(f"   exports {len(own)}")
        groups: dict[str, list[str]] = {}
        free: list[str] = []
        for name in own:
            if "::" in name:
                owner, method = name.split("::", 1)
                groups.setdefault(owner, []).append(method)
            else:
                free.append(name)
        if free:
            lines.append("     " + ", ".join(free))
        for owner in sorted(groups):
            methods = ", ".join(sorted(set(groups[owner])))
            lines.append(f"     {owner}: {methods}")
        game_imports = {
            dll: names
            for dll, names in m["imports"].items()
            if dll.lower() not in SYSTEM_DLLS
        }
        if game_imports:
            lines.append("   game imports")
            for dll in sorted(game_imports):
                names = ", ".join(game_imports[dll])
                lines.append(f"     {dll}: {names}")
        if m["rtti"]:
            lines.append("   rtti " + ", ".join(m["rtti"]))
        if m["sources"]:
            lines.append("   sources " + ", ".join(m["sources"]))
        if m["logs"]:
            lines.append("   logs")
            for name in m["logs"]:
                lines.append(f"     {name}")
    return "\n".join(lines).rstrip() + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path, help="PE file or directory")
    parser.add_argument(
        "--json",
        type=Path,
        help="write the machine-readable survey here instead of a text report",
    )
    args = parser.parse_args()
    undec = Undecorator()
    modules: list[dict] = []
    for root in args.paths:
        for path in iter_files(root):
            try:
                modules.append(survey_file(path, undec))
            except ValueError as exc:
                modules.append({"path": str(path), "error": str(exc)})
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps(modules, ensure_ascii=False, indent=1), encoding="utf-8"
        )
        print(f"wrote {args.json} ({len(modules)} modules)", file=sys.stderr)
        return
    sys.stdout.write(format_report(modules))


if __name__ == "__main__":
    main()
