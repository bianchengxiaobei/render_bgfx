#!/usr/bin/env python3
"""Compile the HBG shader variants and write shaders/manifest.json."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import threading
import time
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence

_EXECUTOR: Optional[ThreadPoolExecutor] = None
_FUTURES: List[Future[None]] = []
_BUILD_TIMEOUT: Optional[float] = None
_OPTIMIZATION = 3
_PROGRESS_ENABLED = True
_ONLY_FILTER = ""
_WRITE_MANIFEST = True
_PROGRESS_LOCK = threading.Lock()
_PROGRESS = {"scheduled": 0, "completed": 0, "cached": 0, "compiled": 0, "failed": 0, "total": 0}
_FAILURES: List["BuildFailure"] = []


@dataclass
class BuildFailure:
    source: str
    backend: str
    shader_type: str
    output: str
    elapsed: float
    reason: str
    command: str
    stdout: str
    stderr: str


VARIANT_BITS = (
    (0, "HAS_NORMAL_MAP"),
    (1, "HAS_MASK_MAP"),
    (2, "ALPHA_CLIP"),
    (3, "DOUBLE_SIDED"),
)
BACKENDS = {
    "dxbc": ["--platform", "windows", "-p", "s_5_0"],
    "spirv": ["--platform", "linux", "-p", "spirv"],
}


def compile_shader(shaderc: Path, root: Path, bgfx_src: Path, source: Path, output: Path,
                   shader_type: str, backend: str, defines: Sequence[str], force: bool,
                   newest_input: float, varying_def: Optional[Path] = None) -> None:
    if _EXECUTOR is None:
        raise RuntimeError("shader compile executor is not initialized")
    output_display = output.relative_to(root).as_posix()
    if _ONLY_FILTER and _ONLY_FILTER not in output_display:
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    source_display = source.relative_to(root).as_posix()
    newest = max(newest_input, source.stat().st_mtime)
    with _PROGRESS_LOCK:
        _PROGRESS["scheduled"] += 1
    if not force and output.exists() and output.stat().st_size > 0 and output.stat().st_mtime >= newest:
        with _PROGRESS_LOCK:
            _PROGRESS["completed"] += 1
            _PROGRESS["cached"] += 1
        return
    command: List[str] = [
        str(shaderc),
        "-f", str(source),
        "-o", str(output),
        "--type", shader_type,
        "-i", str(root / "shaders"),
        "-i", str(root / "shaders/include"),
        "-i", str(bgfx_src / "src"),
        *BACKENDS[backend],
        "-O", str(_OPTIMIZATION),
        "--Werror",
    ]
    if shader_type != "compute":
        command.extend(["--varyingdef", str(varying_def or (root / "shaders/include/varying.def.sc"))])
    if defines:
        command.extend(["--define", ";".join(defines)])

    def run() -> None:
        started = time.monotonic()
        failure_reason: Optional[str] = None
        stdout = ""
        stderr = ""
        try:
            result = subprocess.run(command, cwd=root, text=True, capture_output=True,
                                    timeout=_BUILD_TIMEOUT)
        except subprocess.TimeoutExpired as error:
            failure_reason = f"timeout after {_BUILD_TIMEOUT:g}s"
            stdout = error.stdout.decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
            stderr = error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else (error.stderr or "")
        except OSError as error:
            failure_reason = f"failed to launch shaderc: {error}"
        else:
            if result.returncode != 0:
                failure_reason = f"exit code {result.returncode}"
                stdout = result.stdout
                stderr = result.stderr
        elapsed = time.monotonic() - started
        if failure_reason is not None:
            failure = BuildFailure(
                source=source_display,
                backend=backend,
                shader_type=shader_type,
                output=output_display,
                elapsed=elapsed,
                reason=failure_reason,
                command=subprocess.list2cmdline(command),
                stdout=stdout[-4000:],
                stderr=stderr[-4000:],
            )
            with _PROGRESS_LOCK:
                _PROGRESS["failed"] += 1
                _FAILURES.append(failure)
        else:
            with _PROGRESS_LOCK:
                _PROGRESS["compiled"] += 1
        with _PROGRESS_LOCK:
            _PROGRESS["completed"] += 1
            completed = _PROGRESS["completed"]
            total = _PROGRESS["total"]
        if _PROGRESS_ENABLED:
            prefix = f"[{completed}/{total}]" if total > 0 else f"[{completed}]"
            status = "FAILED" if failure_reason is not None else "ok"
            print(f"shader build {prefix} {source_display} ({backend}, {shader_type}) "
                  f"{status} {elapsed:.2f}s", flush=True)

    _FUTURES.append(_EXECUTOR.submit(run))



def variant_defines(mask: int) -> List[str]:
    return [f"{name}=1" for bit, name in VARIANT_BITS if mask & (1 << bit)]


def manifest_paths(node: object) -> List[str]:
    paths: List[str] = []
    if isinstance(node, str):
        paths.append(node)
    elif isinstance(node, dict):
        for value in node.values():
            paths.extend(manifest_paths(value))
    elif isinstance(node, list):
        for value in node:
            paths.extend(manifest_paths(value))
    return paths


def validate_manifest_outputs(root: Path, manifest: Dict[str, object]) -> List[str]:
    missing: List[str] = []
    seen: set[str] = set()
    for relative in manifest_paths(manifest["passes"]):
        if not relative.startswith("build/") or relative in seen:
            continue
        seen.add(relative)
        output = root / "shaders" / relative
        if not output.is_file() or output.stat().st_size == 0:
            missing.append(relative)
    return missing


def clean_stale_outputs(root: Path, manifest: Dict[str, object]) -> List[str]:
    expected = {root / "shaders" / relative for relative in manifest_paths(manifest["passes"])
                if relative.startswith("build/")}
    removed: List[str] = []
    for backend in BACKENDS:
        backend_root = root / "shaders" / "build" / backend
        if not backend_root.is_dir():
            continue
        for output in backend_root.rglob("*.bin"):
            if output not in expected:
                output.unlink()
                removed.append(output.relative_to(root).as_posix())
    return removed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--shaderc", type=Path, required=True)
    parser.add_argument("--bgfx-src", type=Path, required=True)
    parser.add_argument("--force", action="store_true", help="ignore cached outputs")
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)))
    parser.add_argument("--optimization", type=int, choices=range(4), default=3,
                        help="shaderc optimization level (default: 3; lower levels are not release builds)")
    parser.add_argument("--timeout", type=float, default=600.0,
                        help="per-variant timeout in seconds; 0 disables (default: 600)")
    parser.add_argument("--no-progress", action="store_true")
    parser.add_argument("--only", default="",
                        help="diagnostic: build only outputs whose path contains this text")
    parser.add_argument("--no-manifest", action="store_true",
                        help="diagnostic: do not write or validate shaders/manifest.json")
    args = parser.parse_args()
    if args.jobs < 1:
        print("--jobs must be at least 1", file=sys.stderr)
        return 1
    if args.timeout < 0:
        print("--timeout must be zero or positive", file=sys.stderr)
        return 1
    if args.only and not args.no_manifest:
        print("--only requires --no-manifest", file=sys.stderr)
        return 1
    if args.optimization < 3:
        print(f"warning: shader build optimization is O{args.optimization}, not release O3", file=sys.stderr)

    root = args.root.resolve()
    shaderc = args.shaderc.resolve()
    bgfx_src = args.bgfx_src.resolve()
    if not shaderc.exists():
        print(f"shaderc not found: {shaderc}", file=sys.stderr)
        return 1

    global _BUILD_TIMEOUT, _OPTIMIZATION, _PROGRESS_ENABLED, _ONLY_FILTER, _WRITE_MANIFEST
    _BUILD_TIMEOUT = args.timeout if args.timeout > 0 else None
    _OPTIMIZATION = args.optimization
    _PROGRESS_ENABLED = not args.no_progress
    _ONLY_FILTER = args.only
    _WRITE_MANIFEST = not args.no_manifest
    started_at = time.monotonic()

    manifest: Dict[str, object] = {
        "version": 1,
        "generated_by": "tools/shader_build.py",
        "optimization": args.optimization,
        "passes": {},
    }

    passes = manifest["passes"]
    assert isinstance(passes, dict)

    source_dirs = {
        root / "shaders" / "passes",
        root / "shaders" / "post",
        root / "shaders" / "ui",
    }

    def is_compiled_source(path: Path) -> bool:
        return path.parent in source_dirs and not path.name.endswith(".def.sc")

    shader_inputs = [path for path in (root / "shaders").rglob("*.sc") if not is_compiled_source(path)]
    shader_inputs.extend([
        bgfx_src / "src" / "bgfx_shader.sh",
        bgfx_src / "src" / "bgfx_compute.sh",
    ])
    newest_input = max((path.stat().st_mtime for path in shader_inputs if path.exists()), default=0.0)

    global _EXECUTOR
    _EXECUTOR = ThreadPoolExecutor(max_workers=args.jobs)
    for backend in BACKENDS:
        backend_root = root / "shaders" / "build" / backend
        forward_vertex = backend_root / "forward.vs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward.vs.sc",
                       forward_vertex, "vertex", backend, [], args.force, newest_input)
        forward_instanced_vertex = backend_root / "forward_instanced.vs.bin"
        instanced_varying_def = root / "shaders/include/varying_instanced.def.sc"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward_instanced.vs.sc",
                       forward_instanced_vertex, "vertex", backend, [], args.force, newest_input,
                       instanced_varying_def)
        forward_fragments = []
        forward_buffer_fragments = []
        for mask in range(1 << len(VARIANT_BITS)):
            fragment = backend_root / f"forward.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward.fs.sc",
                           fragment, "fragment", backend, variant_defines(mask) + ["HBG_SHADOW_POINT_SAMPLER=1", "HBG_CONTACT_SHADOW_TEXTURE=1"],
                           args.force, newest_input)
            forward_fragments.append(fragment.relative_to(root / "shaders").as_posix())
            buffer_fragment = backend_root / f"forward_buffer.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward.fs.sc",
                           buffer_fragment, "fragment", backend,
                           variant_defines(mask) + ["HBG_SHADOW_POINT_SAMPLER=1", "HBG_CONTACT_SHADOW_TEXTURE=1",
                                                    "HBG_LIGHT_LIST_BUFFER=1"],
                           args.force, newest_input)
            forward_buffer_fragments.append(buffer_fragment.relative_to(root / "shaders").as_posix())

        prepass_fragments = []
        for mask in range(1 << len(VARIANT_BITS)):
            fragment = backend_root / f"prepass.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/prepass.fs.sc",
                           fragment, "fragment", backend, variant_defines(mask), args.force,
                           newest_input)
            prepass_fragments.append(fragment.relative_to(root / "shaders").as_posix())

        gbuffer_fragments = []
        for mask in range(1 << len(VARIANT_BITS)):
            fragment = backend_root / f"gbuffer.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/gbuffer.fs.sc",
                           fragment, "fragment", backend, variant_defines(mask), args.force,
                           newest_input)
            gbuffer_fragments.append(fragment.relative_to(root / "shaders").as_posix())
        for mask in range(1 << len(VARIANT_BITS)):
            fragment = backend_root / f"gbuffer.fs.{16 + mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/gbuffer.fs.sc",
                           fragment, "fragment", backend,
                           variant_defines(mask) + ["HAS_LIGHT_LAYERS=1"], args.force, newest_input)
            gbuffer_fragments.append(fragment.relative_to(root / "shaders").as_posix())

        gbuffer_clear_fragment = backend_root / "gbuffer_clear.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/gbuffer_clear.fs.sc",
                       gbuffer_clear_fragment, "fragment", backend, [], args.force,
                       newest_input)

        gbuffer_decals_fragments = []
        for mask in range(1 << len(VARIANT_BITS)):
            fragment = backend_root / f"gbuffer_decals.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/gbuffer.fs.sc",
                           fragment, "fragment", backend, variant_defines(mask) + ["HAS_DECALS=1"],
                           args.force, newest_input)
            gbuffer_decals_fragments.append(fragment.relative_to(root / "shaders").as_posix())
        for mask in range(1 << len(VARIANT_BITS)):
            fragment = backend_root / f"gbuffer_decals.fs.{16 + mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/gbuffer.fs.sc",
                           fragment, "fragment", backend,
                           variant_defines(mask) + ["HAS_DECALS=1", "HAS_LIGHT_LAYERS=1"],
                           args.force, newest_input)
            gbuffer_decals_fragments.append(fragment.relative_to(root / "shaders").as_posix())

        decal_fragment = backend_root / "decal.fs.bin"
        decal_clear_fragment = backend_root / "decal_clear.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/decal.fs.sc",
                       decal_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/decal_clear.fs.sc",
                       decal_clear_fragment, "fragment", backend, [], args.force, newest_input)

        deferred_lighting_fragment = backend_root / "deferred_lighting.fs.bin"
        deferred_lighting_buffer_fragment = backend_root / "deferred_lighting_buffer.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting.fs.sc",
                       deferred_lighting_fragment, "fragment", backend, ["HBG_SHADOW_POINT_SAMPLER=1"],
                       args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting.fs.sc",
                       deferred_lighting_buffer_fragment, "fragment", backend,
                       ["HBG_SHADOW_POINT_SAMPLER=1", "HBG_LIGHT_LIST_BUFFER=1"], args.force, newest_input)

        ssao_fragment = backend_root / "ssao.fs.bin"
        ssao_blur_fragment = backend_root / "ssao_blur.fs.bin"
        ssao_upsample_fragment = backend_root / "ssao_upsample.fs.bin"
        ssr_trace_fragment = backend_root / "ssr_trace.fs.bin"
        ssr_composite_fragment = backend_root / "ssr_composite.fs.bin"
        ssgi_trace_fragment = backend_root / "ssgi_trace.fs.bin"
        ssgi_denoise_fragment = backend_root / "ssgi_denoise.fs.bin"
        ssgi_composite_fragment = backend_root / "ssgi_composite.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssao.fs.sc",
                       ssao_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssao_blur.fs.sc",
                       ssao_blur_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssao_upsample.fs.sc",
                       ssao_upsample_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssr_trace.fs.sc",
                       ssr_trace_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssr_composite.fs.sc",
                       ssr_composite_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssgi_trace.fs.sc",
                       ssgi_trace_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssgi_denoise.fs.sc",
                       ssgi_denoise_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssgi_composite.fs.sc",
                       ssgi_composite_fragment, "fragment", backend, [], args.force, newest_input)

        motion_varying_def = root / "shaders/include/motion_varying.def.sc"
        motion_vertex = backend_root / "motion_vectors.vs.bin"
        motion_fragment = backend_root / "motion_vectors.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/motion_vectors.vs.sc",
                       motion_vertex, "vertex", backend, [], args.force, newest_input,
                       motion_varying_def)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/motion_vectors.fs.sc",
                       motion_fragment, "fragment", backend, [], args.force, newest_input,
                       motion_varying_def)

        camera_motion_vertex = backend_root / "camera_motion_vectors.vs.bin"
        camera_motion_fragment = backend_root / "camera_motion_vectors.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/camera_motion_vectors.vs.sc",
                       camera_motion_vertex, "vertex", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/camera_motion_vectors.fs.sc",
                       camera_motion_fragment, "fragment", backend, [], args.force, newest_input)

        depth_pyramid_vertex = backend_root / "depth_pyramid.vs.bin"
        depth_pyramid_copy_fragment = backend_root / "depth_pyramid_copy.fs.bin"
        depth_pyramid_downsample_fragment = backend_root / "depth_pyramid_downsample.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/depth_pyramid.vs.sc",
                       depth_pyramid_vertex, "vertex", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/depth_pyramid_copy.fs.sc",
                       depth_pyramid_copy_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/depth_pyramid_downsample.fs.sc",
                       depth_pyramid_downsample_fragment, "fragment", backend, [], args.force,
                       newest_input)

        sky_fragment = backend_root / "sky.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/sky.fs.sc",
                       sky_fragment, "fragment", backend, [], args.force, newest_input)

        volumetric_fog_fragment = backend_root / "volumetric_fog.fs.bin"
        volumetric_fog_buffer_fragment = backend_root / "volumetric_fog_buffer.fs.bin"
        volumetric_fog_composite_fragment = backend_root / "volumetric_fog_composite.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/volumetric_fog.fs.sc",
                       volumetric_fog_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/volumetric_fog.fs.sc",
                       volumetric_fog_buffer_fragment, "fragment", backend, ["HBG_LIGHT_LIST_BUFFER=1"],
                       args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/volumetric_fog_composite.fs.sc",
                       volumetric_fog_composite_fragment, "fragment", backend, [], args.force, newest_input)

        debug_normal_fragment = backend_root / "debug_normal.fs.bin"
        debug_motion_fragment = backend_root / "debug_motion_vectors.fs.bin"
        debug_depth_fragment = backend_root / "debug_depth.fs.bin"
        debug_depth_pyramid_fragment = backend_root / "debug_depth_pyramid.fs.bin"
        debug_light_cluster_fragment = backend_root / "debug_light_cluster.fs.bin"
        debug_light_cluster_buffer_fragment = backend_root / "debug_light_cluster_buffer.fs.bin"
        debug_shadow_atlas_fragment = backend_root / "debug_shadow_atlas.fs.bin"
        debug_color_fragment = backend_root / "debug_color.fs.bin"
        debug_ssao_fragment = backend_root / "debug_ssao.fs.bin"
        debug_ssr_fragment = backend_root / "debug_ssr.fs.bin"
        debug_decals_fragment = backend_root / "debug_decals.fs.bin"
        debug_ssgi_fragment = backend_root / "debug_ssgi.fs.bin"
        debug_gbuffer_fragment = backend_root / "debug_gbuffer.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_normal.fs.sc",
                       debug_normal_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_motion_vectors.fs.sc",
                       debug_motion_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_depth.fs.sc",
                       debug_depth_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_depth_pyramid.fs.sc",
                       debug_depth_pyramid_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_light_cluster.fs.sc",
                       debug_light_cluster_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_light_cluster.fs.sc",
                       debug_light_cluster_buffer_fragment, "fragment", backend, ["HBG_LIGHT_LIST_BUFFER=1"],
                       args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_shadow_atlas.fs.sc",
                       debug_shadow_atlas_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_color.fs.sc",
                       debug_color_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_ssao.fs.sc",
                       debug_ssao_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_ssr.fs.sc",
                       debug_ssr_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_decals.fs.sc",
                       debug_decals_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_ssgi.fs.sc",
                       debug_ssgi_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_gbuffer.fs.sc",
                       debug_gbuffer_fragment, "fragment", backend, [], args.force, newest_input)
        debug_contact_shadows_fragment = backend_root / "debug_contact_shadows.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_contact_shadows.fs.sc",
                       debug_contact_shadows_fragment, "fragment", backend, [], args.force, newest_input)
        debug_shadow_cascade_fragment = backend_root / "debug_shadow_cascade.fs.bin"
        debug_shadow_filter_fragment = backend_root / "debug_shadow_filter.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_shadow_cascade.fs.sc",
                       debug_shadow_cascade_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_shadow_filter.fs.sc",
                       debug_shadow_filter_fragment, "fragment", backend, [], args.force, newest_input)

        contact_shadows_compute = backend_root / "contact_shadows.cs.bin"
        contact_shadows_buffer_compute = backend_root / "contact_shadows_buffer.cs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/contact_shadows.cs.sc",
                       contact_shadows_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/contact_shadows.cs.sc",
                       contact_shadows_buffer_compute, "compute", backend, ["HBG_LIGHT_LIST_BUFFER=1"],
                       args.force, newest_input)

        light_screen_aabb_compute = backend_root / "light_screen_aabb.cs.bin"
        light_big_tile_compute = backend_root / "light_big_tile.cs.bin"
        light_fptl_compute = backend_root / "light_fptl.cs.bin"
        light_cluster_compute = backend_root / "light_cluster.cs.bin"
        light_screen_aabb_buffer_compute = backend_root / "light_screen_aabb_buffer.cs.bin"
        light_big_tile_buffer_compute = backend_root / "light_big_tile_buffer.cs.bin"
        light_fptl_buffer_compute = backend_root / "light_fptl_buffer.cs.bin"
        light_cluster_buffer_compute = backend_root / "light_cluster_buffer.cs.bin"
        light_indirect_args_compute = backend_root / "light_indirect_args.cs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/light_screen_aabb.cs.sc",
                       light_screen_aabb_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/light_big_tile.cs.sc",
                       light_big_tile_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/light_fptl.cs.sc",
                       light_fptl_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/light_cluster.cs.sc",
                       light_cluster_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/light_screen_aabb_buffer.cs.sc",
                       light_screen_aabb_buffer_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/light_big_tile_buffer.cs.sc",
                       light_big_tile_buffer_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/light_fptl_buffer.cs.sc",
                       light_fptl_buffer_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/light_cluster_buffer.cs.sc",
                       light_cluster_buffer_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/light_indirect_args.cs.sc",
                       light_indirect_args_compute, "compute", backend, [], args.force, newest_input)

        debug_albedo_fragments = []
        debug_roughness_fragments = []
        for mask in range(1 << len(VARIANT_BITS)):
            albedo_fragment = backend_root / f"debug_albedo.fs.{mask}.bin"
            roughness_fragment = backend_root / f"debug_roughness.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_material.fs.sc",
                           albedo_fragment, "fragment", backend, variant_defines(mask), args.force,
                           newest_input)
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_material.fs.sc",
                           roughness_fragment, "fragment", backend, variant_defines(mask) + ["DEBUG_ROUGHNESS=1"],
                           args.force, newest_input)
            debug_albedo_fragments.append(albedo_fragment.relative_to(root / "shaders").as_posix())
            debug_roughness_fragments.append(roughness_fragment.relative_to(root / "shaders").as_posix())

        tonemap_vertex = backend_root / "tonemap.vs.bin"
        tonemap_fragment = backend_root / "tonemap.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/tonemap.vs.sc",
                       tonemap_vertex, "vertex", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/tonemap.fs.sc",
                       tonemap_fragment, "fragment", backend, [], args.force, newest_input)

        final_fragments = []
        for name, defines in (("final.fs.bin", []), ("final.fs.fxaa.bin", ["HBG_FXAA=1"])):
            fragment = backend_root / name
            compile_shader(shaderc, root, bgfx_src, root / "shaders/post/final.fs.sc",
                           fragment, "fragment", backend, defines, args.force, newest_input)
            final_fragments.append(fragment.relative_to(root / "shaders").as_posix())

        uber_post_fragment = backend_root / "uber_post.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/uber_post.fs.sc",
                       uber_post_fragment, "fragment", backend, [], args.force, newest_input)

        debug_color_grading_fragment = backend_root / "debug_color_grading.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/debug_color_grading.fs.sc",
                       debug_color_grading_fragment, "fragment", backend, [], args.force, newest_input)

        bloom_prefilter_fragments = []
        for name, defines in (("bloom_prefilter.fs.low.bin", []), ("bloom_prefilter.fs.hq.bin", ["BLOOM_HQ=1"])):
            fragment = backend_root / name
            compile_shader(shaderc, root, bgfx_src, root / "shaders/post/bloom_prefilter.fs.sc",
                           fragment, "fragment", backend, defines, args.force, newest_input)
            bloom_prefilter_fragments.append(fragment.relative_to(root / "shaders").as_posix())

        bloom_blur_fragments = []
        for name, defines in (("bloom_blur.fs.h.bin", []), ("bloom_blur.fs.v.bin", ["BLOOM_VERTICAL=1"])):
            fragment = backend_root / name
            compile_shader(shaderc, root, bgfx_src, root / "shaders/post/bloom_blur.fs.sc",
                           fragment, "fragment", backend, defines, args.force, newest_input)
            bloom_blur_fragments.append(fragment.relative_to(root / "shaders").as_posix())

        bloom_downsample_fragment = backend_root / "bloom_downsample.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/bloom_downsample.fs.sc",
                       bloom_downsample_fragment, "fragment", backend, [], args.force, newest_input)

        bloom_upsample_fragments = []
        for name, defines in (("bloom_upsample.fs.tent.bin", []), ("bloom_upsample.fs.bicubic.bin", ["BLOOM_BICUBIC=1"])):
            fragment = backend_root / name
            compile_shader(shaderc, root, bgfx_src, root / "shaders/post/bloom_upsample.fs.sc",
                           fragment, "fragment", backend, defines, args.force, newest_input)
            bloom_upsample_fragments.append(fragment.relative_to(root / "shaders").as_posix())

        debug_bloom_fragment = backend_root / "debug_bloom.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/debug_bloom.fs.sc",
                       debug_bloom_fragment, "fragment", backend, [], args.force, newest_input)

        taa_fragment = backend_root / "taa.fs.bin"
        taa_sharpen_fragment = backend_root / "taa_sharpen.fs.bin"
        debug_taa_history_fragment = backend_root / "debug_taa_history.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/taa.fs.sc",
                       taa_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/taa_sharpen.fs.sc",
                       taa_sharpen_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/debug_taa_history.fs.sc",
                       debug_taa_history_fragment, "fragment", backend, [], args.force, newest_input)

        dof_coc_fragment = backend_root / "dof_coc.fs.bin"
        dof_gather_fragments = []
        for name, defines in (("dof_gather.fs.low.bin", []), ("dof_gather.fs.high.bin", ["DOF_HQ=1"])):
            fragment = backend_root / name
            compile_shader(shaderc, root, bgfx_src, root / "shaders/post/dof_gather.fs.sc",
                           fragment, "fragment", backend, defines, args.force, newest_input)
            dof_gather_fragments.append(fragment.relative_to(root / "shaders").as_posix())
        dof_combine_fragment = backend_root / "dof_combine.fs.bin"
        debug_dof_coc_fragment = backend_root / "debug_dof_coc.fs.bin"
        debug_fxaa_fragment = backend_root / "debug_fxaa.fs.bin"
        motion_blur_fragment = backend_root / "motion_blur.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/dof_coc.fs.sc",
                       dof_coc_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/dof_combine.fs.sc",
                       dof_combine_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/debug_dof_coc.fs.sc",
                       debug_dof_coc_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/debug_fxaa.fs.sc",
                       debug_fxaa_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/motion_blur.fs.sc",
                       motion_blur_fragment, "fragment", backend, [], args.force, newest_input)

        postprocess_exposure_compute = backend_root / "postprocess_exposure.cs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/postprocess_exposure.cs.sc",
                       postprocess_exposure_compute, "compute", backend, [], args.force, newest_input)

        exposure_prepass_compute = backend_root / "exposure_prepass.cs.bin"
        exposure_reduce_compute = backend_root / "exposure_reduce.cs.bin"
        exposure_reduce_final_compute = backend_root / "exposure_reduce_final.cs.bin"
        histogram_clear_compute = backend_root / "histogram_clear.cs.bin"
        histogram_gen_compute = backend_root / "histogram_gen.cs.bin"
        histogram_reduce_compute = backend_root / "histogram_reduce.cs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/exposure_prepass.cs.sc",
                       exposure_prepass_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/exposure_reduce.cs.sc",
                       exposure_reduce_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/exposure_reduce_final.cs.sc",
                       exposure_reduce_final_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/histogram_clear.cs.sc",
                       histogram_clear_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/histogram_gen.cs.sc",
                       histogram_gen_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/histogram_reduce.cs.sc",
                       histogram_reduce_compute, "compute", backend, [], args.force, newest_input)

        apply_exposure_fragment = backend_root / "apply_exposure.fs.bin"
        debug_exposure_fragment = backend_root / "debug_exposure.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/apply_exposure.fs.sc",
                       apply_exposure_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/post/debug_exposure.fs.sc",
                       debug_exposure_fragment, "fragment", backend, [], args.force, newest_input)

        shadow_vertex = backend_root / "shadowcaster.vs.bin"
        shadow_varying_def = root / "shaders/include/varying_shadow.def.sc"
        shadow_instanced_varying_def = root / "shaders/include/varying_shadow_instanced.def.sc"
        shadow_fragments = []
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadowcaster.vs.sc",
                       shadow_vertex, "vertex", backend, [], args.force, newest_input,
                       shadow_varying_def)
        shadow_instanced_vertex = backend_root / "shadowcaster_instanced.vs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadowcaster_instanced.vs.sc",
                       shadow_instanced_vertex, "vertex", backend, [], args.force, newest_input,
                       shadow_instanced_varying_def)
        for mask in range(1 << len(VARIANT_BITS)):
            fragment = backend_root / f"shadowcaster.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadowcaster.fs.sc",
                           fragment, "fragment", backend, variant_defines(mask), args.force,
                           newest_input, shadow_varying_def)
            shadow_fragments.append(fragment.relative_to(root / "shaders").as_posix())

        shadow_cull_compute = backend_root / "shadow_cull.cs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadow_cull.cs.sc",
                       shadow_cull_compute, "compute", backend, [], args.force, newest_input)

        debug_vertex = backend_root / "debug_light_data.vs.bin"
        debug_fragment = backend_root / "debug_light_data.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_light_data.vs.sc",
                       debug_vertex, "vertex", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_light_data.fs.sc",
                       debug_fragment, "fragment", backend, [], args.force, newest_input)

        wireframe_vertex = backend_root / "wireframe.vs.bin"
        wireframe_fragment = backend_root / "wireframe.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/wireframe.vs.sc",
                       wireframe_vertex, "vertex", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/wireframe.fs.sc",
                       wireframe_fragment, "fragment", backend, [], args.force, newest_input)

        ui_vertex = backend_root / "imgui.vs.bin"
        ui_fragment = backend_root / "imgui.fs.bin"
        ui_varying_def = root / "shaders/ui/varying.def.sc"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/ui/imgui.vs.sc",
                       ui_vertex, "vertex", backend, [], args.force, newest_input, ui_varying_def)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/ui/imgui.fs.sc",
                       ui_fragment, "fragment", backend, [], args.force, newest_input, ui_varying_def)

        backend_entry = {
            "forward": {
                "vertex": forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": forward_fragments,
                "bufferFragments": forward_buffer_fragments,
            },
            "forwardInstanced": {
                "vertex": forward_instanced_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": forward_fragments,
                "bufferFragments": forward_buffer_fragments,
            },
            "tonemap": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": tonemap_fragment.relative_to(root / "shaders").as_posix(),
            },
            "final": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": final_fragments,
            },
            "uberPost": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": uber_post_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugColorGrading": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_color_grading_fragment.relative_to(root / "shaders").as_posix(),
            },
            "bloomPrefilter": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": bloom_prefilter_fragments,
            },
            "bloomBlur": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": bloom_blur_fragments,
            },
            "bloomDownsample": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": bloom_downsample_fragment.relative_to(root / "shaders").as_posix(),
            },
            "bloomUpsample": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": bloom_upsample_fragments,
            },
            "debugBloom": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_bloom_fragment.relative_to(root / "shaders").as_posix(),
            },
            "taa": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": taa_fragment.relative_to(root / "shaders").as_posix(),
            },
            "taaSharpen": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": taa_sharpen_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugTaaHistory": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_taa_history_fragment.relative_to(root / "shaders").as_posix(),
            },
            "dofCoc": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": dof_coc_fragment.relative_to(root / "shaders").as_posix(),
            },
            "dofGather": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": dof_gather_fragments,
            },
            "dofCombine": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": dof_combine_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugDofCoc": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_dof_coc_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugFxaa": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_fxaa_fragment.relative_to(root / "shaders").as_posix(),
            },
            "motionBlur": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": motion_blur_fragment.relative_to(root / "shaders").as_posix(),
            },
            "postProcessExposure": {
                "compute": postprocess_exposure_compute.relative_to(root / "shaders").as_posix(),
            },
            "postProcessExposurePrepass": {
                "compute": exposure_prepass_compute.relative_to(root / "shaders").as_posix(),
            },
            "postProcessExposureReduce": {
                "compute": exposure_reduce_compute.relative_to(root / "shaders").as_posix(),
            },
            "postProcessExposureReduceFinal": {
                "compute": exposure_reduce_final_compute.relative_to(root / "shaders").as_posix(),
            },
            "postProcessHistogramClear": {
                "compute": histogram_clear_compute.relative_to(root / "shaders").as_posix(),
            },
            "postProcessHistogramGen": {
                "compute": histogram_gen_compute.relative_to(root / "shaders").as_posix(),
            },
            "postProcessHistogramReduce": {
                "compute": histogram_reduce_compute.relative_to(root / "shaders").as_posix(),
            },
            "applyExposure": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": apply_exposure_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugExposure": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_exposure_fragment.relative_to(root / "shaders").as_posix(),
            },
            "shadow": {
                "vertex": shadow_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": shadow_fragments,
            },
            "shadowInstanced": {
                "vertex": shadow_instanced_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": shadow_fragments,
            },
            "shadowCull": {
                "compute": shadow_cull_compute.relative_to(root / "shaders").as_posix(),
            },
            "prepass": {
                "vertex": forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": prepass_fragments,
            },
            "gbuffer": {
                "vertex": forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": gbuffer_fragments,
            },
            "gbufferClear": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": gbuffer_clear_fragment.relative_to(root / "shaders").as_posix(),
            },
            "gbufferDecals": {
                "vertex": forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": gbuffer_decals_fragments,
            },
            "decal": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": decal_fragment.relative_to(root / "shaders").as_posix(),
            },
            "decalClear": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": decal_clear_fragment.relative_to(root / "shaders").as_posix(),
            },
            "deferredLighting": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": deferred_lighting_fragment.relative_to(root / "shaders").as_posix(),
                "bufferFragment": deferred_lighting_buffer_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssao": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssao_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssaoBlur": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssao_blur_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssaoUpsample": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssao_upsample_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssrTrace": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssr_trace_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssrComposite": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssr_composite_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssgiTrace": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssgi_trace_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssgiDenoise": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssgi_denoise_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssgiComposite": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssgi_composite_fragment.relative_to(root / "shaders").as_posix(),
            },
            "motionVectors": {
                "vertex": motion_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": motion_fragment.relative_to(root / "shaders").as_posix(),
            },
            "cameraMotionVectors": {
                "vertex": camera_motion_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": camera_motion_fragment.relative_to(root / "shaders").as_posix(),
            },
            "depthPyramidCopy": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": depth_pyramid_copy_fragment.relative_to(root / "shaders").as_posix(),
            },
            "depthPyramidDownsample": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": depth_pyramid_downsample_fragment.relative_to(root / "shaders").as_posix(),
            },
            "sky": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": sky_fragment.relative_to(root / "shaders").as_posix(),
            },
            "volumetricFog": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": volumetric_fog_fragment.relative_to(root / "shaders").as_posix(),
                "bufferFragment": volumetric_fog_buffer_fragment.relative_to(root / "shaders").as_posix(),
            },
            "volumetricFogComposite": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": volumetric_fog_composite_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugLightData": {
                "vertex": debug_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugNormal": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_normal_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugMotionVectors": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_motion_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugDepth": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_depth_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugDepthPyramid": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_depth_pyramid_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugLightCluster": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_light_cluster_fragment.relative_to(root / "shaders").as_posix(),
                "bufferFragment": debug_light_cluster_buffer_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugShadowAtlas": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_shadow_atlas_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugColor": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_color_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugSsao": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_ssao_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugSsr": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_ssr_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugDecals": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_decals_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugSsgi": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_ssgi_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugGbuffer": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_gbuffer_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugContactShadows": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_contact_shadows_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugShadowCascade": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_shadow_cascade_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugShadowFilter": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_shadow_filter_fragment.relative_to(root / "shaders").as_posix(),
            },
            "contactShadows": {
                "compute": contact_shadows_compute.relative_to(root / "shaders").as_posix(),
                "bufferCompute": contact_shadows_buffer_compute.relative_to(root / "shaders").as_posix(),
            },
            "lightScreenAabb": {
                "compute": light_screen_aabb_compute.relative_to(root / "shaders").as_posix(),
            },
            "lightBigTile": {
                "compute": light_big_tile_compute.relative_to(root / "shaders").as_posix(),
            },
            "lightFptl": {
                "compute": light_fptl_compute.relative_to(root / "shaders").as_posix(),
            },
            "lightCluster": {
                "compute": light_cluster_compute.relative_to(root / "shaders").as_posix(),
            },
            "lightScreenAabbBuffer": {
                "compute": light_screen_aabb_buffer_compute.relative_to(root / "shaders").as_posix(),
            },
            "lightBigTileBuffer": {
                "compute": light_big_tile_buffer_compute.relative_to(root / "shaders").as_posix(),
            },
            "lightFptlBuffer": {
                "compute": light_fptl_buffer_compute.relative_to(root / "shaders").as_posix(),
            },
            "lightClusterBuffer": {
                "compute": light_cluster_buffer_compute.relative_to(root / "shaders").as_posix(),
            },
            "lightIndirectArgs": {
                "compute": light_indirect_args_compute.relative_to(root / "shaders").as_posix(),
            },
            "debugAlbedo": {
                "vertex": forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": debug_albedo_fragments,
            },
            "debugRoughness": {
                "vertex": forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": debug_roughness_fragments,
            },
            "wireframe": {
                "vertex": wireframe_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": wireframe_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ui": {
                "vertex": ui_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ui_fragment.relative_to(root / "shaders").as_posix(),
            },
        }
        passes[backend] = backend_entry

    with _PROGRESS_LOCK:
        _PROGRESS["total"] = _PROGRESS["scheduled"]
    for future in _FUTURES:
        future.result()
    _EXECUTOR.shutdown()
    _EXECUTOR = None
    _FUTURES.clear()

    elapsed = time.monotonic() - started_at
    with _PROGRESS_LOCK:
        compiled = _PROGRESS["compiled"]
        cached = _PROGRESS["cached"]
        failed = _PROGRESS["failed"]
        scheduled = _PROGRESS["scheduled"]
    print(f"shader build summary: {scheduled} variants, {compiled} compiled, {cached} cached, "
          f"{failed} failed, {elapsed:.1f}s")

    if _FAILURES:
        for failure in _FAILURES:
            print(f"shader build failed: {failure.source} ({failure.backend}, {failure.shader_type}) "
                  f"after {failure.elapsed:.1f}s: {failure.reason}", file=sys.stderr)
            print(f"  output: {failure.output}", file=sys.stderr)
            print(f"  command: {failure.command}", file=sys.stderr)
            if failure.stdout:
                print(failure.stdout, file=sys.stderr)
            if failure.stderr:
                print(failure.stderr, file=sys.stderr)
        return 1

    if not _WRITE_MANIFEST:
        return 0

    missing = validate_manifest_outputs(root, manifest)
    if missing:
        for relative in missing:
            print(f"shader build missing or empty output: {relative}", file=sys.stderr)
        return 1
    stale = clean_stale_outputs(root, manifest)
    if stale:
        print(f"shader build pruned {len(stale)} stale output(s)")

    manifest_path = root / "shaders" / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"shader build: {manifest_path}")
    return 0



if __name__ == "__main__":
    raise SystemExit(main())
