#!/usr/bin/env python3
"""Compile the HBG shader variants and write shaders/manifest.json."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence

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
    output.parent.mkdir(parents=True, exist_ok=True)
    if not force and output.exists() and output.stat().st_mtime >= newest_input:
        return
    command: List[str] = [
        str(shaderc),
        "-f", str(source),
        "-o", str(output),
        "--type", shader_type,
        "--varyingdef", str(varying_def or (root / "shaders/include/varying.def.sc")),
        "-i", str(root / "shaders"),
        "-i", str(root / "shaders/include"),
        "-i", str(bgfx_src / "src"),
        *BACKENDS[backend],
        "-O", "3",
        "--Werror",
    ]
    if defines:
        command.extend(["--define", ";".join(defines)])
    result = subprocess.run(command, cwd=root, text=True, capture_output=True)
    if result.returncode != 0:
        sys.stderr.write(f"shader build failed: {source} ({backend}, {shader_type})\n")
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)


def variant_defines(mask: int) -> List[str]:
    return [f"{name}=1" for bit, name in VARIANT_BITS if mask & (1 << bit)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--shaderc", type=Path, required=True)
    parser.add_argument("--bgfx-src", type=Path, required=True)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    root = args.root.resolve()
    shaderc = args.shaderc.resolve()
    bgfx_src = args.bgfx_src.resolve()
    if not shaderc.exists():
        print(f"shaderc not found: {shaderc}", file=sys.stderr)
        return 1

    manifest: Dict[str, object] = {
        "version": 1,
        "generated_by": "tools/shader_build.py",
        "passes": {},
    }
    passes = manifest["passes"]
    assert isinstance(passes, dict)

    shader_inputs = list((root / "shaders").rglob("*.sc"))
    shader_inputs.extend([shaderc, bgfx_src / "src" / "bgfx_shader.sh"])
    newest_input = max((path.stat().st_mtime for path in shader_inputs if path.exists()), default=0.0)

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
        for mask in range(1 << len(VARIANT_BITS)):
            fragment = backend_root / f"forward.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward.fs.sc",
                           fragment, "fragment", backend, variant_defines(mask), args.force,
                           newest_input)
            forward_fragments.append(fragment.relative_to(root / "shaders").as_posix())

        prepass_fragments = []
        for mask in range(1 << len(VARIANT_BITS)):
            fragment = backend_root / f"prepass.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/prepass.fs.sc",
                           fragment, "fragment", backend, variant_defines(mask), args.force,
                           newest_input)
            prepass_fragments.append(fragment.relative_to(root / "shaders").as_posix())

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

        debug_normal_fragment = backend_root / "debug_normal.fs.bin"
        debug_motion_fragment = backend_root / "debug_motion_vectors.fs.bin"
        debug_depth_fragment = backend_root / "debug_depth.fs.bin"
        debug_depth_pyramid_fragment = backend_root / "debug_depth_pyramid.fs.bin"
        debug_light_cluster_fragment = backend_root / "debug_light_cluster.fs.bin"
        debug_shadow_atlas_fragment = backend_root / "debug_shadow_atlas.fs.bin"
        debug_color_fragment = backend_root / "debug_color.fs.bin"
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
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_shadow_atlas.fs.sc",
                       debug_shadow_atlas_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_color.fs.sc",
                       debug_color_fragment, "fragment", backend, [], args.force, newest_input)

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

        shadow_vertex = backend_root / "shadowcaster.vs.bin"
        shadow_fragment = backend_root / "shadowcaster.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadowcaster.vs.sc",
                       shadow_vertex, "vertex", backend, [], args.force, newest_input)
        shadow_instanced_vertex = backend_root / "shadowcaster_instanced.vs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadowcaster_instanced.vs.sc",
                       shadow_instanced_vertex, "vertex", backend, [], args.force, newest_input,
                       instanced_varying_def)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadowcaster.fs.sc",
                       shadow_fragment, "fragment", backend, [], args.force, newest_input)

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
            },
            "forwardInstanced": {
                "vertex": forward_instanced_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": forward_fragments,
            },
            "tonemap": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": tonemap_fragment.relative_to(root / "shaders").as_posix(),
            },
            "shadow": {
                "vertex": shadow_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": shadow_fragment.relative_to(root / "shaders").as_posix(),
            },
            "shadowInstanced": {
                "vertex": shadow_instanced_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": shadow_fragment.relative_to(root / "shaders").as_posix(),
            },
            "prepass": {
                "vertex": forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": prepass_fragments,
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
            },
            "debugShadowAtlas": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_shadow_atlas_fragment.relative_to(root / "shaders").as_posix(),
            },
            "debugColor": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_color_fragment.relative_to(root / "shaders").as_posix(),
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

    manifest_path = root / "shaders" / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"shader build: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
