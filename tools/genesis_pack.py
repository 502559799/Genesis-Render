#!/usr/bin/env python3
"""
Genesis Render - offline asset package tool (.gpkg)

A package is one file that carries everything a scene needs at run time:

    package.json            package metadata (name, scene entry, capabilities)
    scenes/*.scene.json     ECS / materials / physics description
    materials/*.json        standalone material definitions
    shaders/**              GLSL source + SPIR-V + MSL (all three backends)
    textures/**             PNG / JPG / HDR
    scripts/**              Lua 5.4 and JavaScript (V8) scripts
    config/**               gameplay / render configuration

Usage:
    python3 tools/genesis_pack.py build  SRC_DIR [-o OUT.gpkg] [--level 9]
    python3 tools/genesis_pack.py validate SRC_DIR|FILE.gpkg
    python3 tools/genesis_pack.py list    FILE.gpkg
    python3 tools/genesis_pack.py extract FILE.gpkg OUT_DIR

Binary format (little endian), mirrored by src/genesis/resource/asset_package.cpp:

    char     magic[8]        "GENPKG01"
    uint32   version         1
    uint32   file_count
    uint32   manifest_size
    char     manifest[...]   generated JSON
    repeated file_count times:
        uint32 path_size
        char   path[...]
        uint32 data_offset   relative to the data blob
        uint32 stored_size
        uint32 raw_size
        uint32 crc32         of the raw bytes
        uint8  compression   0 stored, 1 zlib
        uint8  reserved[3]
    uint32   data_size
    char     data[...]
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"GENPKG01"
VERSION = 1
MANIFEST_NAMES = {"package.json"}


def collect_files(src: Path) -> list[Path]:
    files: list[Path] = []
    for path in sorted(src.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(src)
        if any(part.startswith(".") for part in rel.parts):
            continue
        if path.suffix.lower() == ".gpkg":
            continue
        files.append(path)
    return files


def package_manifest(src: Path, files: list[Path]) -> dict:
    manifest: dict = {}
    package_json = src / "package.json"
    if package_json.is_file():
        try:
            manifest = json.loads(package_json.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            raise SystemExit(f"package.json is not valid JSON: {exc}")
    manifest.setdefault("name", src.name.replace("_src", ""))
    manifest.setdefault("format", "genesis-gpkg")
    manifest["version_format"] = VERSION
    entries = []
    for path in files:
        rel = path.relative_to(src).as_posix()
        data = path.read_bytes()
        entries.append({"path": rel, "size": len(data)})
    manifest["files"] = entries
    return manifest


def validate_source(src: Path, verbose: bool = True) -> list[str]:
    problems: list[str] = []
    files = [p.relative_to(src).as_posix() for p in collect_files(src)]
    if not files:
        return [f"{src}: no files"]
    scenes = [f for f in files if f.startswith("scenes/") and f.endswith(".scene.json")]
    materials = [f for f in files if f.startswith("materials/") and f.endswith(".json")]
    textures = [f for f in files if f.startswith("textures/")]
    scripts = [f for f in files if f.startswith("scripts/") and (f.endswith(".lua") or f.endswith(".js"))]
    configs = [f for f in files if f.startswith("config/")]
    shaders = [f for f in files if f.startswith("shaders/")]

    if not scenes:
        problems.append("missing scenes/*.scene.json")
    if not materials:
        problems.append("missing materials/*.json")
    if not textures:
        problems.append("missing textures/")
    if not scripts:
        problems.append("missing scripts/*.lua or *.js")
    if not configs:
        problems.append("missing config/")
    if not shaders:
        problems.append("missing shaders/")
    else:
        lowered = [f.lower() for f in shaders]
        has_glsl = any(f.endswith((".glsl", ".vert", ".frag", ".comp")) for f in lowered)
        has_spv = any(f.endswith(".spv") for f in lowered)
        has_msl = any(f.endswith(".metal") for f in lowered)
        if not has_glsl:
            problems.append("shaders/: no GLSL source (three-backend package)")
        if not has_spv:
            problems.append("shaders/: no SPIR-V (.spv)")
        if not has_msl:
            problems.append("shaders/: no Metal (.metal)")

    # Every JSON asset must parse.
    for rel in files:
        if rel.endswith(".json"):
            try:
                json.loads((src / rel).read_text(encoding="utf-8"))
            except json.JSONDecodeError as exc:
                problems.append(f"{rel}: invalid JSON ({exc})")

    # The scene entry declared in package.json must exist.
    package_json = src / "package.json"
    if package_json.is_file():
        try:
            meta = json.loads(package_json.read_text(encoding="utf-8"))
            scene = meta.get("scene")
            if scene and scene not in files:
                problems.append(f"package.json scene '{scene}' not found in package")
        except json.JSONDecodeError:
            pass
    if verbose:
        print(f"  scenes    : {len(scenes)}")
        print(f"  materials : {len(materials)}")
        print(f"  shaders   : {len(shaders)}")
        print(f"  textures  : {len(textures)}")
        print(f"  scripts   : {len(scripts)}")
        print(f"  configs   : {len(configs)}")
        print(f"  total     : {len(files)} files")
    return problems


def build(src: Path, out: Path, level: int) -> None:
    problems = validate_source(src)
    if problems:
        print("validation failed:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        raise SystemExit(2)
    files = collect_files(src)
    manifest = package_manifest(src, files)
    manifest_bytes = json.dumps(manifest, indent=2, ensure_ascii=False).encode("utf-8")

    blobs: list[bytes] = []
    table: list[bytes] = []
    offset = 0
    stored_total = 0
    for path in files:
        rel = path.relative_to(src).as_posix()
        raw = path.read_bytes()
        compressed = zlib.compress(raw, level)
        use_compression = 1 if len(compressed) < len(raw) else 0
        stored = compressed if use_compression else raw
        name = rel.encode("utf-8")
        table.append(
            struct.pack("<I", len(name))
            + name
            + struct.pack("<IIII", offset, len(stored), len(raw), zlib.crc32(raw) & 0xFFFFFFFF)
            + struct.pack("<B3s", use_compression, b"\x00\x00\x00")
        )
        blobs.append(stored)
        offset += len(stored)
        stored_total += len(stored)

    header = MAGIC + struct.pack("<III", VERSION, len(files), len(manifest_bytes))
    data = b"".join(blobs)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("wb") as handle:
        handle.write(header)
        handle.write(manifest_bytes)
        for chunk in table:
            handle.write(chunk)
        handle.write(struct.pack("<I", len(data)))
        handle.write(data)

    print(f"wrote {out} ({out.stat().st_size} bytes, {len(files)} files, {stored_total} stored bytes)")
    print(f"raw payload: {sum(len(b) for b in blobs)} bytes")


def read_package(path: Path):
    raw = path.read_bytes()
    if len(raw) < 20 or raw[:8] != MAGIC:
        raise SystemExit(f"{path}: not a Genesis package")
    version, count, manifest_size = struct.unpack_from("<III", raw, 8)
    if version != VERSION:
        raise SystemExit(f"{path}: unsupported version {version}")
    cursor = 20
    manifest = raw[cursor : cursor + manifest_size]
    cursor += manifest_size
    entries = []
    for _ in range(count):
        (name_size,) = struct.unpack_from("<I", raw, cursor)
        cursor += 4
        name = raw[cursor : cursor + name_size].decode("utf-8")
        cursor += name_size
        offset, stored, raw_size, crc = struct.unpack_from("<IIII", raw, cursor)
        cursor += 16
        compression = raw[cursor]
        cursor += 4
        entries.append((name, offset, stored, raw_size, crc, compression))
    (data_size,) = struct.unpack_from("<I", raw, cursor)
    cursor += 4
    data = raw[cursor : cursor + data_size]
    return manifest, entries, data


def cmd_list(path: Path) -> None:
    manifest, entries, data = read_package(path)
    meta = json.loads(manifest.decode("utf-8"))
    print(f"package : {meta.get('name', path.name)}")
    print(f"version : {meta.get('version', '-')}")
    print(f"scene   : {meta.get('scene', '-')}")
    print(f"files   : {len(entries)}")
    for name, offset, stored, raw_size, crc, compression in entries:
        print(f"  {name:<52} {raw_size:>9} B  {'zlib' if compression else 'raw '}  crc={crc:08x}")
    print(f"payload : {len(data)} bytes")


def cmd_extract(path: Path, out: Path) -> None:
    manifest, entries, data = read_package(path)
    out.mkdir(parents=True, exist_ok=True)
    for name, offset, stored, raw_size, crc, compression in entries:
        chunk = data[offset : offset + stored]
        blob = zlib.decompress(chunk) if compression else chunk
        if len(blob) != raw_size or (zlib.crc32(blob) & 0xFFFFFFFF) != crc:
            raise SystemExit(f"{name}: CRC / size mismatch")
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(blob)
    (out / "PACKAGE_MANIFEST.json").write_bytes(manifest)
    print(f"extracted {len(entries)} files to {out}")


def cmd_validate(arg: Path) -> int:
    if arg.is_dir():
        problems = validate_source(arg)
    else:
        manifest, entries, data = read_package(arg)
        problems = []
        names = [e[0] for e in entries]
        for prefix, label in (("scenes/", "scene"), ("materials/", "materials"), ("shaders/", "shaders"),
                              ("textures/", "textures"), ("scripts/", "scripts"), ("config/", "config")):
            if not any(n.startswith(prefix) for n in names):
                problems.append(f"missing {label}")
        for name, offset, stored, raw_size, crc, compression in entries:
            chunk = data[offset : offset + stored]
            blob = zlib.decompress(chunk) if compression else chunk
            if len(blob) != raw_size or (zlib.crc32(blob) & 0xFFFFFFFF) != crc:
                problems.append(f"{name}: CRC / size mismatch")
        try:
            json.loads(manifest.decode("utf-8"))
        except json.JSONDecodeError as exc:
            problems.append(f"manifest: {exc}")
    if problems:
        print("INVALID", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 2
    print("VALID")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    build_parser = sub.add_parser("build", help="build a .gpkg from a source directory")
    build_parser.add_argument("src", type=Path)
    build_parser.add_argument("-o", "--output", type=Path)
    build_parser.add_argument("--level", type=int, default=9)

    validate_parser = sub.add_parser("validate", help="validate a source directory or package")
    validate_parser.add_argument("path", type=Path)

    list_parser = sub.add_parser("list", help="list package contents")
    list_parser.add_argument("path", type=Path)

    extract_parser = sub.add_parser("extract", help="extract a package")
    extract_parser.add_argument("path", type=Path)
    extract_parser.add_argument("out", type=Path)

    args = parser.parse_args()
    if args.command == "build":
        out = args.output or args.src.parent / (args.src.name.replace("_src", "") + ".gpkg")
        build(args.src, out, args.level)
        return 0
    if args.command == "validate":
        return cmd_validate(args.path)
    if args.command == "list":
        cmd_list(args.path)
        return 0
    if args.command == "extract":
        cmd_extract(args.path, args.out)
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
