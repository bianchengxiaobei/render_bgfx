#!/usr/bin/env python3
"""Compile the SG-0 spike shaders and pack them into a minimal SGB1 asset."""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
import time
from pathlib import Path

BACKENDS = {
    "dxbc": ["--platform", "windows", "-p", "s_5_0"],
    "spirv": ["--platform", "linux", "-p", "spirv"],
}
STAGES = (("vertex", "vs", 0), ("fragment", "fs", 1))


def fnv1a64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def align8(value: int) -> int:
    return (value + 7) & ~7


def compile_shader(shaderc: Path, root: Path, bgfx_src: Path, source: Path, output: Path,
                   shader_type: str, backend: str, force: bool, timeout: float) -> None:
    inputs = [
        source,
        root / "shaders/include/varying.def.sc",
        root / "shaders/shadergraph/spike/surface.spike.sc",
    ]
    newest = max(path.stat().st_mtime for path in inputs if path.exists())
    if not force and output.exists() and output.stat().st_size > 0 and output.stat().st_mtime >= newest:
        print(f"cached {output.relative_to(root).as_posix()}")
        return

    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        str(shaderc),
        "-f", str(source),
        "-o", str(output),
        "--type", shader_type,
        "-i", str(root / "shaders"),
        "-i", str(root / "shaders/include"),
        "-i", str(bgfx_src / "src"),
        *BACKENDS[backend],
        "--varyingdef", str(root / "shaders/include/varying.def.sc"),
    ]
    if backend == "spirv":
        command.extend(["-O", "3", "--Werror"])

    started = time.monotonic()
    try:
        result = subprocess.run(command, cwd=root, text=True, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise SystemExit(f"shader compile timeout: {source} ({backend})")
    if result.returncode != 0:
        sys.stderr.write(result.stdout[-4000:])
        sys.stderr.write(result.stderr[-4000:])
        raise SystemExit(f"shader compile failed: {source} ({backend})")
    if not output.exists() or output.stat().st_size == 0:
        raise SystemExit(f"shader compiler produced an empty file: {output}")
    elapsed = time.monotonic() - started
    print(f"compiled {output.relative_to(root).as_posix()} ({backend}, {shader_type}) {elapsed:.2f}s")


def make_sections(blobs: dict[tuple[int, int], bytes]) -> list[tuple[int, bytes]]:
    meta = {
        "name": "sg_spike",
        "target": "lit",
        "generatorVersion": "sg0",
        "schemaVersion": 1,
        "nodeSpecVersion": 1,
        "stageBudget": 5,
        "requiredFeatureMask": 0,
    }
    passes = [
        {
            "id": 0,
            "name": "forward",
            "stages": {
                "graph0": 0,
                "graph1": 1,
                "graph2": 2,
                "graph3": 3,
                "graph4": 4,
                "lightData": 5,
                "directionalShadow": 6,
                "lightList": 7,
                "shadowContext": 8,
                "punctualShadow": 9,
                "environment": 10,
                "contactShadow": 11,
                "probeMaps": 14,
                "cookieAtlas": 15,
            },
        }
    ]
    meta_bytes = json.dumps(meta, sort_keys=True, separators=(",", ":")).encode("utf-8")
    passes_bytes = json.dumps(passes, sort_keys=True, separators=(",", ":")).encode("utf-8")

    entries = []
    data_offset = 4 + len(blobs) * 12
    payload = bytearray(struct.pack("<I", len(blobs)))
    for (stage, backend), blob in sorted(blobs.items()):
        entries.append(struct.pack("<BBHII", stage, backend, 0, data_offset, len(blob)))
        data_offset += len(blob)
    payload.extend(b"".join(entries))
    for _, blob in sorted(blobs.items()):
        payload.extend(blob)
    return [(1, meta_bytes), (6, passes_bytes), (7, bytes(payload))]


def pack_sgb(sections: list[tuple[int, bytes]]) -> bytes:
    header_size = 28 + len(sections) * 20
    offsets = []
    cursor = header_size
    for _, payload in sections:
        cursor = align8(cursor)
        offsets.append(cursor)
        cursor += len(payload)

    content_hash = fnv1a64(b"".join(payload for _, payload in sections))
    header = struct.pack("<4sHHQQI", b"SGB1", 1, 0, content_hash, 0, len(sections))
    table = bytearray()
    for (kind, payload), offset in zip(sections, offsets):
        table.extend(struct.pack("<IQQ", kind, offset, len(payload)))

    output = bytearray(header)
    output.extend(table)
    for (_, payload), offset in zip(sections, offsets):
        if len(output) < offset:
            output.extend(b"\0" * (offset - len(output)))
        output.extend(payload)
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--shaderc", type=Path, required=True)
    parser.add_argument("--bgfx-src", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--timeout", type=float, default=600.0)
    args = parser.parse_args()

    root = args.root.resolve()
    shaderc = args.shaderc.resolve()
    bgfx_src = args.bgfx_src.resolve()
    output = (args.output or (root / "tests/shadergraph/assets/spike.sgb")).resolve()
    if not shaderc.exists():
        raise SystemExit(f"shaderc not found: {shaderc}")
    if not (root / "shaders/shadergraph/spike/surface.spike.sc").exists():
        raise SystemExit("missing generated surface include; run sg_generate_surface")

    blobs: dict[tuple[int, int], bytes] = {}
    for backend, backend_id in (("dxbc", 0), ("spirv", 1)):
        for stage_name, stage_suffix, stage_id in STAGES:
            output_path = root / "shaders/build" / backend / f"sg_spike.{stage_suffix}.bin"
            compile_shader(
                shaderc,
                root,
                bgfx_src,
                root / "shaders/shadergraph/spike" / f"forward_graph.{stage_suffix}.sc",
                output_path,
                stage_name,
                backend,
                args.force,
                args.timeout,
            )
            blobs[(stage_id, backend_id)] = output_path.read_bytes()

    packed = pack_sgb(make_sections(blobs))
    output.parent.mkdir(parents=True, exist_ok=True)
    if args.check:
        if not output.exists():
            raise SystemExit(f"cannot check missing asset: {output}")
        if output.read_bytes() != packed:
            raise SystemExit(f"SGB is not deterministic: {output}")
        print(f"SGB check ok: {output.relative_to(root).as_posix()}")
        return 0
    output.write_bytes(packed)
    print(
        f"wrote {output.relative_to(root).as_posix()} "
        f"({len(packed)} bytes, hash=0x{fnv1a64(packed):016x})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
