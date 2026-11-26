#!/usr/bin/env python3
"""Compile the HBG shader variants and write shaders/manifest.json."""

from __future__ import annotations

import argparse
import atexit
import json
import os
import subprocess
import sys
import threading
import time
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Dict, List, Optional, Sequence

_EXECUTOR: Optional[ThreadPoolExecutor] = None
_FUTURES: List[Future[None]] = []
_BUILD_TIMEOUT: Optional[float] = None
_OPTIMIZATION = 3
_RETRIES = 1
_PROGRESS_ENABLED = True
_ONLY_FILTER = ""
_WRITE_MANIFEST = True
_PROGRESS_LOCK = threading.Lock()
_PROGRESS = {"scheduled": 0, "completed": 0, "cached": 0, "compiled": 0, "failed": 0, "total": 0}
_FAILURES: List["BuildFailure"] = []
_BUILD_LOCK: Optional[BinaryIO] = None
_PLAN_ONLY = False
_PLAN: List["CompileTask"] = []


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


@dataclass
class CompileTask:
    source: str
    output: str
    stage: str
    backend: str
    defines: List[str]
    varying_def: Optional[str]
    d3d_skip_optimization: bool
    optimization: int

    def to_json(self) -> Dict[str, object]:
        return {
            "source": self.source,
            "output": self.output,
            "stage": self.stage,
            "backend": self.backend,
            "defines": self.defines,
            "varyingDef": self.varying_def,
            "d3dSkipOptimization": self.d3d_skip_optimization,
            "optimization": self.optimization,
        }


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
INCLUDE_DIRS = ("shaders", "shaders/include", "{bgfxSrc}/src")


def acquire_build_lock(root: Path) -> BinaryIO:
    """Serialize shader builds that share shaders/build.

    Multiple CMake targets (or a manual shader build racing an IDE build) must
    not write the same .bin files concurrently. The lock is held by the OS and
    is released automatically if the process dies.
    """
    lock_path = root / "shaders" / "build" / ".shader_build.lock"
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    handle = open(lock_path, "a+b")
    handle.seek(0, os.SEEK_END)
    if handle.tell() == 0:
        handle.write(b"0")
        handle.flush()
    handle.seek(0)
    waited = False
    while True:
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
            return handle
        except OSError:
            if os.name != "nt":
                handle.close()
                raise
            if not waited:
                print("shader build: waiting for another shader build to finish", flush=True)
                waited = True
            time.sleep(0.5)


def compile_shader(shaderc: Path, root: Path, bgfx_src: Path, source: Path, output: Path,
                   shader_type: str, backend: str, defines: Sequence[str], force: bool,
                   newest_input: float, varying_def: Optional[Path] = None,
                   d3d_skip_optimization: bool = False,
                   optimization: Optional[int] = None) -> None:
    effective_optimization = (0 if backend == "dxbc" and d3d_skip_optimization
                              else (_OPTIMIZATION if optimization is None else optimization))
    source_display = source.relative_to(root).as_posix()
    output_display = output.relative_to(root).as_posix()
    _PLAN.append(CompileTask(
        source=source_display,
        output=output.relative_to(root / "shaders").as_posix(),
        stage=shader_type,
        backend=backend,
        defines=list(defines),
        varying_def=(None if shader_type == "compute" or varying_def is None
                     else varying_def.relative_to(root).as_posix()),
        d3d_skip_optimization=d3d_skip_optimization,
        optimization=effective_optimization,
    ))
    if _PLAN_ONLY:
        return
    if _EXECUTOR is None:
        raise RuntimeError("shader compile executor is not initialized")
    if _ONLY_FILTER and _ONLY_FILTER not in output_display:
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    newest = max(newest_input, source.stat().st_mtime,
                 varying_def.stat().st_mtime if varying_def is not None else 0.0)
    with _PROGRESS_LOCK:
        _PROGRESS["scheduled"] += 1
    if not force and output.exists() and output.stat().st_size > 0 and output.stat().st_mtime >= newest:
        with _PROGRESS_LOCK:
            _PROGRESS["completed"] += 1
            _PROGRESS["cached"] += 1
        return
    temp_output = output.with_name(
        f"{output.name}.{os.getpid()}.{threading.get_ident()}.tmp")
    command: List[str] = [
        str(shaderc),
        "-f", str(source),
        "-o", str(temp_output),
        "--type", shader_type,
    ]
    for include_dir in INCLUDE_DIRS:
        resolved_include = bgfx_src / "src" if include_dir == "{bgfxSrc}/src" else root / include_dir
        command.extend(["-i", str(resolved_include)])
    command.extend(BACKENDS[backend])
    # FXC can spend unbounded time optimizing area-light variants. Omitting -O
    # selects D3DCOMPILE_SKIP_OPTIMIZATION; warnings are not treated as errors
    # for this fallback because FXC reports conservative uninitialized-local
    # warnings that the optimized path suppresses.
    if not (backend == "dxbc" and d3d_skip_optimization):
        command.extend(["-O", str(effective_optimization), "--Werror"])
    if shader_type != "compute":
        command.extend(["--varyingdef", str(varying_def or (root / "shaders/include/varying.def.sc"))])
    if defines:
        command.extend(["--define", ";".join(defines)])
    display_command = [str(output) if part == str(temp_output) else part for part in command]

    def run() -> None:
        started = time.monotonic()
        failure_reason: Optional[str] = None
        stdout = ""
        stderr = ""
        attempts = _RETRIES + 1
        for attempt in range(attempts):
            failure_reason = None
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
                else:
                    try:
                        if not temp_output.is_file() or temp_output.stat().st_size == 0:
                            failure_reason = "empty output"
                        else:
                            os.replace(temp_output, output)
                    except OSError as error:
                        failure_reason = f"failed to publish output: {error}"
            if failure_reason is None:
                break
            try:
                temp_output.unlink()
            except FileNotFoundError:
                pass
            if attempt + 1 < attempts:
                print(f"shader build: retrying {output_display} "
                      f"({attempt + 2}/{attempts}) after {failure_reason}", flush=True)
                time.sleep(0.5)
        elapsed = time.monotonic() - started
        if failure_reason is not None:
            failure = BuildFailure(
                source=source_display,
                backend=backend,
                shader_type=shader_type,
                output=output_display,
                elapsed=elapsed,
                reason=failure_reason,
                command=subprocess.list2cmdline(display_command),
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


def backend_metadata(backend: str) -> Dict[str, str]:
    args = BACKENDS[backend]
    try:
        platform = args[args.index("--platform") + 1]
        profile = args[args.index("-p") + 1]
    except (ValueError, IndexError) as error:
        raise ValueError(f"backend {backend} is missing --platform or -p") from error
    return {"platform": platform, "profile": profile}


def build_plan(passes: Dict[str, object]) -> Dict[str, object]:
    tasks: List[Dict[str, object]] = []
    seen: Dict[str, CompileTask] = {}
    for task in _PLAN:
        key = f"{task.backend}:{task.output}"
        previous = seen.get(key)
        if previous is not None:
            raise ValueError(
                f"duplicate task for {task.backend} {task.output}: "
                f"{previous.source} and {task.source}")
        seen[key] = task
        tasks.append(task.to_json())
    return {
        "version": 1,
        "generatedBy": "tools/shader_build.py --dump-plan",
        "optimization": _OPTIMIZATION,
        "backends": {backend: backend_metadata(backend) for backend in BACKENDS},
        "includeDirs": list(INCLUDE_DIRS),
        "passes": passes,
        "tasks": tasks,
    }


def write_plan(path: Path, plan: Dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(plan, indent=2, ensure_ascii=False) + "\n"
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def describe_plan_value(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def compare_plan(expected: object, actual: object) -> Optional[str]:
    if not isinstance(expected, dict):
        return "expected a JSON object"
    if not isinstance(actual, dict):
        return "generated plan is not a JSON object"
    for key in ("version", "generatedBy", "optimization", "backends", "includeDirs",
                "passes"):
        if expected.get(key) != actual.get(key):
            return (f"{key}: expected {describe_plan_value(expected.get(key))}, "
                    f"got {describe_plan_value(actual.get(key))}")
    expected_tasks = expected.get("tasks")
    actual_tasks = actual.get("tasks")
    if not isinstance(expected_tasks, list):
        return "tasks: expected a JSON array"
    if not isinstance(actual_tasks, list):
        return "tasks: generated plan is not an array"
    if len(expected_tasks) != len(actual_tasks):
        return f"tasks: expected {len(expected_tasks)} entries, got {len(actual_tasks)}"
    fields = ("source", "output", "stage", "backend", "defines", "varyingDef",
              "d3dSkipOptimization", "optimization")
    for index, (expected_task, actual_task) in enumerate(zip(expected_tasks, actual_tasks)):
        if not isinstance(expected_task, dict) or not isinstance(actual_task, dict):
            return f"tasks[{index}]: expected a JSON object"
        if expected_task == actual_task:
            continue
        for field in fields:
            if expected_task.get(field) != actual_task.get(field):
                return (f"tasks[{index}].{field}: expected "
                        f"{describe_plan_value(expected_task.get(field))}, got "
                        f"{describe_plan_value(actual_task.get(field))}")
        return (f"tasks[{index}]: expected {describe_plan_value(expected_task)}, "
                f"got {describe_plan_value(actual_task)}")
    return None


def plan_summary(plan: Dict[str, object]) -> str:
    tasks = plan.get("tasks")
    if not isinstance(tasks, list):
        return "0 tasks"
    counts: Dict[str, int] = {}
    for task in tasks:
        if isinstance(task, dict):
            backend = str(task.get("backend"))
            counts[backend] = counts.get(backend, 0) + 1
    details = ", ".join(f"{backend} {counts.get(backend, 0)}" for backend in BACKENDS)
    return f"{len(tasks)} tasks ({details})"


def plan_relative_path(value: object, context: str, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{context}.{field} must be a non-empty string")
    path = Path(value)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"{context}.{field} must be a safe relative path")
    return value


def load_execution_plan(path: Path) -> tuple[Dict[str, object], List[Dict[str, object]]]:
    try:
        plan = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ValueError(f"failed to read {path}: {error}") from error
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid JSON in {path}: {error}") from error
    if not isinstance(plan, dict):
        raise ValueError(f"{path}: expected a JSON object")
    if plan.get("version") != 1:
        raise ValueError(f"{path}: unsupported version {plan.get('version')!r}")
    passes = plan.get("passes")
    if not isinstance(passes, dict):
        raise ValueError(f"{path}: missing 'passes' metadata; regenerate with --dump-plan")
    raw_tasks = plan.get("tasks")
    if not isinstance(raw_tasks, list):
        raise ValueError(f"{path}: field 'tasks' must be an array")

    tasks: List[Dict[str, object]] = []
    outputs: set[str] = set()
    for index, raw in enumerate(raw_tasks):
        context = f"tasks[{index}]"
        if not isinstance(raw, dict):
            raise ValueError(f"{context} must be an object")
        source = plan_relative_path(raw.get("source"), context, "source")
        output = plan_relative_path(raw.get("output"), context, "output")
        if not output.startswith("build/"):
            raise ValueError(f"{context}.output must start with 'build/'")
        stage = raw.get("stage")
        if stage not in ("vertex", "fragment", "compute"):
            raise ValueError(f"{context}.stage is invalid")
        backend = raw.get("backend")
        if backend not in BACKENDS:
            raise ValueError(f"{context}.backend is invalid")
        defines = raw.get("defines")
        if not isinstance(defines, list) or any(
                not isinstance(define, str) or not define for define in defines):
            raise ValueError(f"{context}.defines must be a string array")
        varying = raw.get("varyingDef")
        if varying is not None:
            varying = plan_relative_path(varying, context, "varyingDef")
        skip = raw.get("d3dSkipOptimization")
        if not isinstance(skip, bool):
            raise ValueError(f"{context}.d3dSkipOptimization must be a boolean")
        optimization = raw.get("optimization")
        if not isinstance(optimization, int) or isinstance(optimization, bool) or optimization < 0:
            raise ValueError(f"{context}.optimization must be a non-negative integer")
        if skip and (backend != "dxbc" or optimization != 0):
            raise ValueError(f"{context}: d3dSkipOptimization requires dxbc and optimization 0")
        key = f"{backend}:{output}"
        if key in outputs:
            raise ValueError(f"{context}: duplicate output {backend} {output}")
        outputs.add(key)
        tasks.append({
            "source": source,
            "output": output,
            "stage": stage,
            "backend": backend,
            "defines": list(defines),
            "varyingDef": varying,
            "d3dSkipOptimization": skip,
            "optimization": optimization,
        })

    metadata_outputs: set[str] = set()
    for backend, entries in passes.items():
        if not isinstance(entries, dict):
            raise ValueError(f"{path}: passes.{backend} must be an object")
        for value in manifest_paths(entries):
            if isinstance(value, str) and value.startswith("build/"):
                metadata_outputs.add(f"{backend}:{value}")
    if outputs != metadata_outputs:
        missing = sorted(outputs - metadata_outputs)
        extra = sorted(metadata_outputs - outputs)
        detail = []
        if missing:
            detail.append(f"missing from passes: {missing[:3]}")
        if extra:
            detail.append(f"not in tasks: {extra[:3]}")
        raise ValueError(f"{path}: passes/tasks mismatch ({'; '.join(detail)})")
    return plan, tasks


def newest_shader_input(root: Path, bgfx_src: Path) -> float:
    source_dirs = {
        root / "shaders" / "passes",
        root / "shaders" / "post",
        root / "shaders" / "ui",
    }

    def is_compiled_source(path: Path) -> bool:
        return path.parent in source_dirs and not path.name.endswith(".def.sc")

    inputs = [path for path in (root / "shaders").rglob("*.sc")
              if not is_compiled_source(path) and "shadergraph" not in path.parts]
    inputs.extend([
        bgfx_src / "src" / "bgfx_shader.sh",
        bgfx_src / "src" / "bgfx_compute.sh",
    ])
    return max((path.stat().st_mtime for path in inputs if path.exists()), default=0.0)


def execute_plan(plan_path: Path, root: Path, shaderc: Path, bgfx_src: Path,
                 args: argparse.Namespace) -> int:
    try:
        plan, tasks = load_execution_plan(plan_path)
    except ValueError as error:
        print(f"shader plan: {error}", file=sys.stderr)
        return 1

    global _EXECUTOR
    _FUTURES.clear()
    _PLAN.clear()
    newest_input = newest_shader_input(root, bgfx_src)
    _EXECUTOR = ThreadPoolExecutor(max_workers=args.jobs)
    started = time.monotonic()
    try:
        for task in tasks:
            varying = (root / task["varyingDef"]
                       if task["varyingDef"] is not None else None)
            compile_shader(
                shaderc,
                root,
                bgfx_src,
                root / str(task["source"]),
                root / "shaders" / str(task["output"]),
                str(task["stage"]),
                str(task["backend"]),
                list(task["defines"]),
                args.force,
                newest_input,
                varying,
                bool(task["d3dSkipOptimization"]),
                int(task["optimization"]),
            )
        with _PROGRESS_LOCK:
            _PROGRESS["total"] = _PROGRESS["scheduled"]
        for future in _FUTURES:
            future.result()
    finally:
        _EXECUTOR.shutdown()
        _EXECUTOR = None
        _FUTURES.clear()

    elapsed = time.monotonic() - started
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

    manifest = {
        "version": 1,
        "generated_by": "tools/shader_build.py --execute-plan",
        "optimization": plan.get("optimization", args.optimization),
        "passes": plan["passes"],
    }
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--shaderc", type=Path,
                        help="shaderc executable (required unless --dump-plan/--plan-check)")
    parser.add_argument("--bgfx-src", type=Path,
                        help="bgfx source directory (required unless --dump-plan/--plan-check)")
    parser.add_argument("--force", action="store_true", help="ignore cached outputs")
    parser.add_argument("--jobs", type=int, default=max(1, min(8, os.cpu_count() or 1)))
    parser.add_argument("--retries", type=int, default=1,
                        help="retry failed variants this many times (default: 1)")
    parser.add_argument("--optimization", type=int, choices=range(4), default=3,
                        help="shaderc optimization level (default: 3; lower levels are not release builds)")
    parser.add_argument("--timeout", type=float, default=1800.0,
                        help="per-variant timeout in seconds; 0 disables (default: 1800)")
    parser.add_argument("--no-progress", action="store_true")
    parser.add_argument("--only", default="",
                        help="diagnostic: build only outputs whose path contains this text")
    parser.add_argument("--no-manifest", action="store_true",
                        help="diagnostic: do not write or validate shaders/manifest.json")
    parser.add_argument("--dump-plan", type=Path, metavar="PATH",
                        help="write the shared compile plan and exit without compiling")
    parser.add_argument("--plan-check", type=Path, metavar="PATH",
                        help="compare the shared compile plan with the build schedule and exit")
    parser.add_argument("--execute-plan", type=Path, metavar="PATH",
                        help="compile tasks and write the manifest from a variants.json plan")
    parser.add_argument("--no-plan", action="store_true",
                        help="use the legacy hardcoded schedule instead of shaders/variants.json")
    args = parser.parse_args()
    plan_only = args.dump_plan is not None or args.plan_check is not None
    if args.dump_plan is not None and args.plan_check is not None:
        print("--dump-plan and --plan-check are mutually exclusive", file=sys.stderr)
        return 1
    if args.execute_plan is not None and plan_only:
        print("--execute-plan cannot be combined with --dump-plan/--plan-check", file=sys.stderr)
        return 1
    if args.jobs < 1:
        print("--jobs must be at least 1", file=sys.stderr)
        return 1
    if args.retries < 0:
        print("--retries must be zero or positive", file=sys.stderr)
        return 1
    if args.timeout < 0:
        print("--timeout must be zero or positive", file=sys.stderr)
        return 1
    if args.only and not args.no_manifest and not plan_only:
        print("--only requires --no-manifest", file=sys.stderr)
        return 1
    if args.optimization < 3:
        print(f"warning: shader build optimization is O{args.optimization}, not release O3", file=sys.stderr)
    if not plan_only:
        if args.shaderc is None:
            print("--shaderc is required unless --dump-plan or --plan-check is used", file=sys.stderr)
            return 1
        if args.bgfx_src is None:
            print("--bgfx-src is required unless --dump-plan or --plan-check is used", file=sys.stderr)
            return 1

    root = args.root.resolve()
    if args.execute_plan is None and not plan_only and not args.no_plan:
        default_plan = root / "shaders" / "variants.json"
        if default_plan.is_file():
            args.execute_plan = default_plan
            print(f"shader plan: executing {default_plan} "
                  "(use --no-plan for the legacy schedule)")
    shaderc = args.shaderc.resolve() if args.shaderc is not None else Path()
    bgfx_src = args.bgfx_src.resolve() if args.bgfx_src is not None else Path()
    if not plan_only and not shaderc.exists():
        print(f"shaderc not found: {shaderc}", file=sys.stderr)
        return 1

    global _BUILD_TIMEOUT, _OPTIMIZATION, _PROGRESS_ENABLED, _ONLY_FILTER, _WRITE_MANIFEST
    global _RETRIES, _BUILD_LOCK, _PLAN_ONLY
    _BUILD_TIMEOUT = args.timeout if args.timeout > 0 else None
    _OPTIMIZATION = args.optimization
    _RETRIES = args.retries
    _PROGRESS_ENABLED = not args.no_progress
    _ONLY_FILTER = args.only
    _WRITE_MANIFEST = not args.no_manifest and not plan_only
    _PLAN_ONLY = plan_only
    _PLAN.clear()
    if not plan_only:
        _BUILD_LOCK = acquire_build_lock(root)
        atexit.register(_BUILD_LOCK.close)
    started_at = time.monotonic()

    if args.execute_plan is not None:
        return execute_plan(args.execute_plan, root, shaderc, bgfx_src, args)

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

    shader_inputs = [path for path in (root / "shaders").rglob("*.sc")
                     if not is_compiled_source(path) and "shadergraph" not in path.parts]
    if plan_only:
        newest_input = 0.0
    else:
        shader_inputs.extend([
            bgfx_src / "src" / "bgfx_shader.sh",
            bgfx_src / "src" / "bgfx_compute.sh",
        ])
        newest_input = max((path.stat().st_mtime for path in shader_inputs if path.exists()),
                           default=0.0)
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
        bindless_forward_vertex = backend_root / "forward_bindless.vs.bin"
        bindless_varying_def = root / "shaders/include/varying_bindless.def.sc"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward_bindless.vs.sc",
                       bindless_forward_vertex, "vertex", backend, [], args.force, newest_input,
                       bindless_varying_def)
        bindless_forward_fragments = []
        bindless_forward_buffer_fragments = []
        for mask in range(1 << 2):
            fragment = backend_root / f"forward_bindless.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward_bindless.fs.sc",
                           fragment, "fragment", backend,
                           variant_defines(mask) + ["HBG_SHADOW_POINT_SAMPLER=1", "HBG_CONTACT_SHADOW_TEXTURE=1"],
                           args.force, newest_input, bindless_varying_def,
                           d3d_skip_optimization=(backend == "dxbc"))
            bindless_forward_fragments.append(fragment.relative_to(root / "shaders").as_posix())
            buffer_fragment = backend_root / f"forward_bindless_buffer.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward_bindless.fs.sc",
                           buffer_fragment, "fragment", backend,
                           variant_defines(mask) + ["HBG_SHADOW_POINT_SAMPLER=1", "HBG_CONTACT_SHADOW_TEXTURE=1",
                                                    "HBG_LIGHT_LIST_BUFFER=1"],
                           args.force, newest_input, bindless_varying_def,
                           d3d_skip_optimization=(backend == "dxbc"))
            bindless_forward_buffer_fragments.append(buffer_fragment.relative_to(root / "shaders").as_posix())
        forward_fragments = []
        forward_buffer_fragments = []
        for mask in range(1 << len(VARIANT_BITS)):
            fragment = backend_root / f"forward.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward.fs.sc",
                           fragment, "fragment", backend, variant_defines(mask) + ["HBG_SHADOW_POINT_SAMPLER=1", "HBG_CONTACT_SHADOW_TEXTURE=1"],
                           args.force, newest_input, d3d_skip_optimization=(backend == "dxbc"))
            forward_fragments.append(fragment.relative_to(root / "shaders").as_posix())
            buffer_fragment = backend_root / f"forward_buffer.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward.fs.sc",
                           buffer_fragment, "fragment", backend,
                           variant_defines(mask) + ["HBG_SHADOW_POINT_SAMPLER=1", "HBG_CONTACT_SHADOW_TEXTURE=1",
                                                    "HBG_LIGHT_LIST_BUFFER=1"],
                           args.force, newest_input, d3d_skip_optimization=(backend == "dxbc"))
            forward_buffer_fragments.append(buffer_fragment.relative_to(root / "shaders").as_posix())

        forward_advanced_fragments = []
        forward_advanced_buffer_fragments = []
        advanced_defines = [
            "ALPHA_CLIP=1", "HBG_ADVANCED_MATERIAL=1",
            "HBG_CONTACT_SHADOW_TEXTURE=1",
        ]
        for hasMaps in (False, True):
            for doubleSided in (False, True):
                suffix = ("_nomap" if not hasMaps else "") + ("_ds" if doubleSided else "")
                defines = advanced_defines + (["DOUBLE_SIDED=1"] if doubleSided else [])
                if hasMaps:
                    defines += ["HAS_NORMAL_MAP=1", "HAS_MASK_MAP=1"]
                fragment = backend_root / f"forward_advanced{suffix}.fs.bin"
                buffer_fragment = backend_root / f"forward_advanced{suffix}_buffer.fs.bin"
                compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward_advanced.fs.sc",
                               fragment, "fragment", backend, defines, args.force, newest_input,
                               d3d_skip_optimization=(backend == "dxbc"))
                compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/forward_advanced.fs.sc",
                               buffer_fragment, "fragment", backend,
                               defines + ["HBG_LIGHT_LIST_BUFFER=1"], args.force, newest_input,
                               d3d_skip_optimization=(backend == "dxbc"))
                forward_advanced_fragments.append(fragment.relative_to(root / "shaders").as_posix())
                forward_advanced_buffer_fragments.append(buffer_fragment.relative_to(root / "shaders").as_posix())

        baked_gi_compensate_fragment = backend_root / "baked_gi_compensate.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/baked_gi_compensate.fs.sc",
                       baked_gi_compensate_fragment, "fragment", backend, [], args.force, newest_input)
        baked_gi_forward_composite_fragment = backend_root / "baked_gi_forward_composite.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/baked_gi_forward_composite.fs.sc",
                       baked_gi_forward_composite_fragment, "fragment", backend, [], args.force, newest_input)

        baked_gi_fragments = []
        for mask in range(1 << len(VARIANT_BITS)):
            fragment = backend_root / f"baked_gi.fs.{mask}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/baked_gi.fs.sc",
                           fragment, "fragment", backend, variant_defines(mask), args.force,
                           newest_input)
            baked_gi_fragments.append(fragment.relative_to(root / "shaders").as_posix())

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
        deferred_lighting_advanced_fragment = backend_root / "deferred_lighting_advanced.fs.bin"
        deferred_lighting_advanced_buffer_fragment = backend_root / "deferred_lighting_advanced_buffer.fs.bin"
        deferred_lighting_advanced_ims_fragment = backend_root / "deferred_lighting_advanced_ims.fs.bin"
        deferred_lighting_advanced_ims_buffer_fragment = backend_root / "deferred_lighting_advanced_ims_buffer.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting.fs.sc",
                       deferred_lighting_fragment, "fragment", backend, ["HBG_SHADOW_POINT_SAMPLER=1"],
                       args.force, newest_input, d3d_skip_optimization=(backend == "dxbc"))
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting.fs.sc",
                       deferred_lighting_buffer_fragment, "fragment", backend,
                       ["HBG_SHADOW_POINT_SAMPLER=1", "HBG_LIGHT_LIST_BUFFER=1"], args.force, newest_input,
                       d3d_skip_optimization=(backend == "dxbc"))
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting_advanced.fs.sc",
                       deferred_lighting_advanced_fragment, "fragment", backend,
                       ["HBG_ADVANCED_MATERIAL=1", "HBG_SHADOW_AREA_SAMPLER=1"], args.force, newest_input,
                       d3d_skip_optimization=(backend == "dxbc"))
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting_advanced.fs.sc",
                       deferred_lighting_advanced_buffer_fragment, "fragment", backend,
                       ["HBG_ADVANCED_MATERIAL=1", "HBG_SHADOW_AREA_SAMPLER=1", "HBG_LIGHT_LIST_BUFFER=1"],
                       args.force, newest_input, d3d_skip_optimization=(backend == "dxbc"))
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting_advanced.fs.sc",
                       deferred_lighting_advanced_ims_fragment, "fragment", backend,
                       ["HBG_ADVANCED_MATERIAL=1", "HBG_SHADOW_AREA_SAMPLER=1", "HBG_SHADOW_IMS_STAGE6=1"],
                       args.force, newest_input, d3d_skip_optimization=(backend == "dxbc"))
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting_advanced.fs.sc",
                       deferred_lighting_advanced_ims_buffer_fragment, "fragment", backend,
                       ["HBG_ADVANCED_MATERIAL=1", "HBG_SHADOW_AREA_SAMPLER=1", "HBG_SHADOW_IMS_STAGE6=1",
                        "HBG_LIGHT_LIST_BUFFER=1"], args.force, newest_input,
                       d3d_skip_optimization=(backend == "dxbc"))

        deferred_variant_fragments = []
        deferred_variant_buffer_fragments = []
        deferred_variant_ims_fragments = []
        deferred_variant_ims_buffer_fragments = []
        deferred_variant_flags = [
            (1, 0), (1, 4), (1, 5), (1, 6), (1, 7), (1, 12), (1, 13), (1, 15),
            (13, 0), (13, 4), (13, 5), (13, 6), (13, 7),
            (17, 0), (17, 4), (17, 5), (17, 6), (17, 7),
            (65, 0), (65, 4), (65, 5), (65, 6), (65, 7),
            (33, 0), (33, 4), (33, 5), (33, 6), (33, 7),
            (125, 15),
        ]
        for variant, (material_flags, light_flags) in enumerate(deferred_variant_flags):
            defines = [f"HBG_VARIANT_MATERIAL_FLAGS={material_flags}",
                       f"HBG_VARIANT_LIGHT_FLAGS={light_flags}"]
            if variant == 28:
                defines.append("HBG_VARIANT_CATCHALL=1")
            if (material_flags & 0x7e) != 0:
                defines.append("HBG_SHADOW_AREA_SAMPLER=1")
            else:
                defines.append("HBG_SHADOW_POINT_SAMPLER=1")
            fragment = backend_root / f"deferred_lighting_variant.fs.{variant}.bin"
            buffer_fragment = backend_root / f"deferred_lighting_variant_buffer.fs.{variant}.bin"
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting_variant.fs.sc",
                           fragment, "fragment", backend, defines, args.force, newest_input,
                           d3d_skip_optimization=(backend == "dxbc"))
            compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting_variant.fs.sc",
                           buffer_fragment, "fragment", backend, defines + ["HBG_LIGHT_LIST_BUFFER=1"],
                           args.force, newest_input, d3d_skip_optimization=(backend == "dxbc"))
            deferred_variant_fragments.append(fragment.relative_to(root / "shaders").as_posix())
            deferred_variant_buffer_fragments.append(buffer_fragment.relative_to(root / "shaders").as_posix())

            if (material_flags & 0x7e) != 0:
                ims_fragment = backend_root / f"deferred_lighting_variant_ims.fs.{variant}.bin"
                ims_buffer_fragment = backend_root / f"deferred_lighting_variant_ims_buffer.fs.{variant}.bin"
                ims_defines = defines + ["HBG_SHADOW_IMS_STAGE6=1"]
                compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting_variant.fs.sc",
                               ims_fragment, "fragment", backend, ims_defines, args.force, newest_input,
                               d3d_skip_optimization=(backend == "dxbc"))
                compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/deferred_lighting_variant.fs.sc",
                               ims_buffer_fragment, "fragment", backend, ims_defines + ["HBG_LIGHT_LIST_BUFFER=1"],
                               args.force, newest_input, d3d_skip_optimization=(backend == "dxbc"))
                deferred_variant_ims_fragments.append(ims_fragment.relative_to(root / "shaders").as_posix())
                deferred_variant_ims_buffer_fragments.append(
                    ims_buffer_fragment.relative_to(root / "shaders").as_posix())
            else:
                deferred_variant_ims_fragments.append(fragment.relative_to(root / "shaders").as_posix())
                deferred_variant_ims_buffer_fragments.append(
                    buffer_fragment.relative_to(root / "shaders").as_posix())

        lit_material_flags_compute = backend_root / "lit_material_flags.cs.bin"
        lit_variant_indirect_clear_compute = backend_root / "lit_variant_indirect_clear.cs.bin"
        lit_variant_indirect_compute = backend_root / "lit_variant_indirect.cs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/lit_material_flags.cs.sc",
                       lit_material_flags_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/lit_variant_indirect_clear.cs.sc",
                       lit_variant_indirect_clear_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/lit_variant_indirect.cs.sc",
                       lit_variant_indirect_compute, "compute", backend, [], args.force, newest_input)

        deferred_variant_computes = []
        deferred_variant_ims_computes = []
        for variant, (material_flags, light_flags) in enumerate(deferred_variant_flags):
            defines = [f"HBG_VARIANT_INDEX={variant}",
                       f"HBG_VARIANT_MATERIAL_FLAGS={material_flags}",
                       f"HBG_VARIANT_LIGHT_FLAGS={light_flags}",
                       "HBG_LIGHT_LIST_BUFFER=1",
                       "HBG_COOKIE_SAMPLER_STAGE=11"]
            if variant == 28:
                defines.append("HBG_VARIANT_CATCHALL=1")
            if (material_flags & 0x7e) != 0:
                defines.append("HBG_SHADOW_AREA_SAMPLER=1")
            else:
                defines.append("HBG_SHADOW_POINT_SAMPLER=1")
            compute = backend_root / f"deferred_lighting_variant_compute.cs.{variant}.bin"
            compile_shader(shaderc, root, bgfx_src,
                           root / "shaders/passes/deferred_lighting_variant_compute.cs.sc",
                           compute, "compute", backend, defines, args.force, newest_input,
                           d3d_skip_optimization=(backend == "dxbc"))
            deferred_variant_computes.append(compute.relative_to(root / "shaders").as_posix())

            if (material_flags & 0x7e) != 0:
                ims_compute = backend_root / f"deferred_lighting_variant_compute_ims.cs.{variant}.bin"
                compile_shader(shaderc, root, bgfx_src,
                               root / "shaders/passes/deferred_lighting_variant_compute.cs.sc",
                               ims_compute, "compute", backend, defines + ["HBG_SHADOW_IMS_STAGE6=1"],
                               args.force, newest_input, d3d_skip_optimization=(backend == "dxbc"))
                deferred_variant_ims_computes.append(ims_compute.relative_to(root / "shaders").as_posix())
            else:
                deferred_variant_ims_computes.append(compute.relative_to(root / "shaders").as_posix())

        ssao_fragment = backend_root / "ssao.fs.bin"
        ssao_blur_fragment = backend_root / "ssao_blur.fs.bin"
        ssao_temporal_fragment = backend_root / "ssao_temporal.fs.bin"
        ssao_upsample_fragment = backend_root / "ssao_upsample.fs.bin"
        ssr_trace_fragment = backend_root / "ssr_trace.fs.bin"
        ssr_temporal_fragment = backend_root / "ssr_temporal.fs.bin"
        ssr_composite_fragment = backend_root / "ssr_composite.fs.bin"
        ssgi_trace_fragment = backend_root / "ssgi_trace.fs.bin"
        ssgi_denoise_fragment = backend_root / "ssgi_denoise.fs.bin"
        ssgi_temporal_fragment = backend_root / "ssgi_temporal.fs.bin"
        ssgi_composite_fragment = backend_root / "ssgi_composite.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssao.fs.sc",
                       ssao_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssao_blur.fs.sc",
                       ssao_blur_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssao_temporal.fs.sc",
                       ssao_temporal_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssao_upsample.fs.sc",
                       ssao_upsample_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssr_trace.fs.sc",
                       ssr_trace_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssr_temporal.fs.sc",
                       ssr_temporal_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssr_composite.fs.sc",
                       ssr_composite_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssgi_trace.fs.sc",
                       ssgi_trace_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssgi_denoise.fs.sc",
                       ssgi_denoise_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/ssgi_temporal.fs.sc",
                       ssgi_temporal_fragment, "fragment", backend, [], args.force, newest_input)
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

        transparent_motion_varying_def = root / "shaders/include/transparent_motion_varying.def.sc"
        transparent_motion_vertex = backend_root / "transparent_motion_vectors.vs.bin"
        transparent_motion_fragment = backend_root / "transparent_motion_vectors.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/transparent_motion_vectors.vs.sc",
                       transparent_motion_vertex, "vertex", backend, [], args.force, newest_input,
                       transparent_motion_varying_def)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/transparent_motion_vectors.fs.sc",
                       transparent_motion_fragment, "fragment", backend, [], args.force, newest_input,
                       transparent_motion_varying_def)

        camera_motion_vertex = backend_root / "camera_motion_vectors.vs.bin"
        camera_motion_fragment = backend_root / "camera_motion_vectors.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/camera_motion_vectors.vs.sc",
                       camera_motion_vertex, "vertex", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/camera_motion_vectors.fs.sc",
                       camera_motion_fragment, "fragment", backend, [], args.force, newest_input)

        depth_pyramid_vertex = backend_root / "depth_pyramid.vs.bin"
        depth_pyramid_copy_fragment = backend_root / "depth_pyramid_copy.fs.bin"
        depth_pyramid_downsample_fragment = backend_root / "depth_pyramid_downsample.fs.bin"
        depth_pyramid_compute = backend_root / "depth_pyramid_compute.cs.bin"
        depth_pyramid_max_compute = backend_root / "depth_pyramid_max_compute.cs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/depth_pyramid.vs.sc",
                       depth_pyramid_vertex, "vertex", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/depth_pyramid_copy.fs.sc",
                       depth_pyramid_copy_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/depth_pyramid_downsample.fs.sc",
                       depth_pyramid_downsample_fragment, "fragment", backend, [], args.force,
                       newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/depth_pyramid_compute.cs.sc",
                       depth_pyramid_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/depth_pyramid_max_compute.cs.sc",
                       depth_pyramid_max_compute, "compute", backend, [], args.force, newest_input)

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
        debug_lightmap_fragment = backend_root / "debug_lightmap.fs.bin"
        debug_ssao_fragment = backend_root / "debug_ssao.fs.bin"
        debug_ssr_fragment = backend_root / "debug_ssr.fs.bin"
        debug_decals_fragment = backend_root / "debug_decals.fs.bin"
        debug_ssgi_fragment = backend_root / "debug_ssgi.fs.bin"
        debug_ssgi_raw_fragment = backend_root / "debug_ssgi_raw.fs.bin"
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
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_lightmap.fs.sc",
                       debug_lightmap_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_ssao.fs.sc",
                       debug_ssao_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_ssr.fs.sc",
                       debug_ssr_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_decals.fs.sc",
                       debug_decals_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_ssgi.fs.sc",
                       debug_ssgi_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/debug_ssgi_raw.fs.sc",
                       debug_ssgi_raw_fragment, "fragment", backend, [], args.force, newest_input)
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
        gpu_driven_cull_compute = backend_root / "gpu_driven_cull.cs.bin"
        gpu_driven_transparent_sort_compute = backend_root / "gpu_driven_transparent_sort.cs.bin"
        skinning_compute = backend_root / "skinning.cs.bin"
        shadow_moments_compute = backend_root / "shadow_moments.cs.bin"
        shadow_sat_horizontal_compute = backend_root / "shadow_sat_horizontal.cs.bin"
        shadow_sat_vertical_compute = backend_root / "shadow_sat_vertical.cs.bin"
        shadow_evsm_blur_compute = backend_root / "shadow_evsm_blur.cs.bin"
        shadow_evsm_blur_moments_compute = backend_root / "shadow_evsm_blur_moments.cs.bin"
        shadow_copy_depth_fragment = backend_root / "shadow_copy_depth.fs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadow_copy_depth.fs.sc",
                       shadow_copy_depth_fragment, "fragment", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadow_cull.cs.sc",
                       shadow_cull_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/gpu_driven_cull.cs.sc",
                       gpu_driven_cull_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/gpu_driven_transparent_sort.cs.sc",
                       gpu_driven_transparent_sort_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/skinning.cs.sc",
                       skinning_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadow_moments.cs.sc",
                       shadow_moments_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadow_sat_horizontal.cs.sc",
                       shadow_sat_horizontal_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadow_sat_vertical.cs.sc",
                       shadow_sat_vertical_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadow_evsm_blur.cs.sc",
                       shadow_evsm_blur_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/passes/shadow_evsm_blur_moments.cs.sc",
                       shadow_evsm_blur_moments_compute, "compute", backend, [], args.force, newest_input)

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

        vfx_spike_varying_def = root / "shaders/vfx/vfx_spike.def.sc"
        vfx_motion_vectors_varying_def = root / "vfx/shaders/vfx_motion_vectors.def.sc"
        vfx_motion_vectors_vertex = backend_root / "vfx_motion_vectors.vs.bin"
        vfx_motion_vectors_fragment = backend_root / "vfx_motion_vectors.fs.bin"
        vfx_shadow_varying_def = root / "vfx/shaders/vfx_shadow.def.sc"
        vfx_shadow_vertex = backend_root / "vfx_shadow.vs.bin"
        vfx_shadow_fragment = backend_root / "vfx_shadow.fs.bin"
        vfx_spike_attributes_compute = backend_root / "vfx_spike_attributes.cs.bin"
        vfx_spike_indirect_compute = backend_root / "vfx_spike_indirect.cs.bin"
        vfx_spike_audit_compute = backend_root / "vfx_spike_audit.cs.bin"
        vfx_spike_vertex = backend_root / "vfx_spike.vs.bin"
        vfx_spike_unlit_fragment = backend_root / "vfx_spike_unlit.fs.bin"
        vfx_spike_lit_fragment = backend_root / "vfx_spike_lit.fs.bin"
        vfx_attributes_test_compute = backend_root / "vfx_attributes_test.cs.bin"
        vfx_attributes_test_vertex = backend_root / "vfx_attributes_test.vs.bin"
        vfx_expressions_test_compute = backend_root / "vfx_expressions_test.cs.bin"
        compile_shader(shaderc, root, bgfx_src, root / "shaders/vfx/vfx_spike_attributes.cs.sc",
                       vfx_spike_attributes_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/vfx/vfx_spike_indirect.cs.sc",
                       vfx_spike_indirect_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/vfx/vfx_attributes_test.cs.sc",
                       vfx_attributes_test_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/vfx/vfx_attributes_test.vs.sc",
                       vfx_attributes_test_vertex, "vertex", backend, [], args.force, newest_input,
                       vfx_spike_varying_def)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/vfx/vfx_expressions_test.cs.sc",
                       vfx_expressions_test_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/vfx/vfx_spike_audit.cs.sc",
                       vfx_spike_audit_compute, "compute", backend, [], args.force, newest_input)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/vfx/vfx_spike.vs.sc",
                       vfx_spike_vertex, "vertex", backend, [], args.force, newest_input,
                       vfx_spike_varying_def)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/vfx/vfx_spike_unlit.fs.sc",
                       vfx_spike_unlit_fragment, "fragment", backend, [], args.force, newest_input,
                       vfx_spike_varying_def)
        compile_shader(shaderc, root, bgfx_src, root / "shaders/vfx/vfx_spike_lit.fs.sc",
                       vfx_spike_lit_fragment, "fragment", backend,
                       ["HBG_CONTACT_SHADOW_TEXTURE=1"], args.force, newest_input,
                       vfx_spike_varying_def, d3d_skip_optimization=(backend == "dxbc"))
        compile_shader(shaderc, root, bgfx_src, root / "vfx/shaders/vfx_motion_vectors.vs.sc",
                       vfx_motion_vectors_vertex, "vertex", backend, [], args.force, newest_input,
                       vfx_motion_vectors_varying_def)
        compile_shader(shaderc, root, bgfx_src, root / "vfx/shaders/vfx_motion_vectors.fs.sc",
                       vfx_motion_vectors_fragment, "fragment", backend, [], args.force, newest_input,
                       vfx_motion_vectors_varying_def)
        compile_shader(shaderc, root, bgfx_src, root / "vfx/shaders/vfx_shadow.vs.sc",
                       vfx_shadow_vertex, "vertex", backend, [], args.force, newest_input,
                       vfx_shadow_varying_def)
        compile_shader(shaderc, root, bgfx_src, root / "vfx/shaders/vfx_shadow.fs.sc",
                       vfx_shadow_fragment, "fragment", backend, [], args.force, newest_input,
                       vfx_shadow_varying_def)

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
            "bindlessForward": {
                "vertex": bindless_forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": bindless_forward_fragments,
                "bufferFragments": bindless_forward_buffer_fragments,
            },
            "forwardAdvanced": {
                "vertex": forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": forward_advanced_fragments,
                "bufferFragments": forward_advanced_buffer_fragments,
            },
            "bakedGI": {
                "vertex": forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": baked_gi_fragments,
            },
            "bakedGICompensate": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": baked_gi_compensate_fragment.relative_to(root / "shaders").as_posix(),
            },
            "bakedGIForwardComposite": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": baked_gi_forward_composite_fragment.relative_to(root / "shaders").as_posix(),
            },
            "tonemap": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": tonemap_fragment.relative_to(root / "shaders").as_posix(),
            },
            "final": {
                "vertex": tonemap_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": final_fragments,
            },
            "vfxSpike": {
                "vertex": vfx_spike_vertex.relative_to(root / "shaders").as_posix(),
                "unlit": vfx_spike_unlit_fragment.relative_to(root / "shaders").as_posix(),
                "lit": vfx_spike_lit_fragment.relative_to(root / "shaders").as_posix(),
                "attributes": vfx_spike_attributes_compute.relative_to(root / "shaders").as_posix(),
                "attributesLayout": vfx_attributes_test_compute.relative_to(root / "shaders").as_posix(),
                "attributesLayoutVertex": vfx_attributes_test_vertex.relative_to(root / "shaders").as_posix(),
                "expressions": vfx_expressions_test_compute.relative_to(root / "shaders").as_posix(),
                "indirect": vfx_spike_indirect_compute.relative_to(root / "shaders").as_posix(),
                "audit": vfx_spike_audit_compute.relative_to(root / "shaders").as_posix(),
                "motionVectorVertex": vfx_motion_vectors_vertex.relative_to(root / "shaders").as_posix(),
                "motionVectorFragment": vfx_motion_vectors_fragment.relative_to(root / "shaders").as_posix(),
                "shadowVertex": vfx_shadow_vertex.relative_to(root / "shaders").as_posix(),
                "shadowFragment": vfx_shadow_fragment.relative_to(root / "shaders").as_posix(),
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
            "gpuDrivenCull": {
                "compute": gpu_driven_cull_compute.relative_to(root / "shaders").as_posix(),
            },
            "gpuDrivenTransparentSort": {
                "compute": gpu_driven_transparent_sort_compute.relative_to(root / "shaders").as_posix(),
            },
            "skinning": {
                "compute": skinning_compute.relative_to(root / "shaders").as_posix(),
            },
            "shadowMoments": {
                "compute": shadow_moments_compute.relative_to(root / "shaders").as_posix(),
            },
            "shadowSatHorizontal": {
                "compute": shadow_sat_horizontal_compute.relative_to(root / "shaders").as_posix(),
            },
            "shadowSatVertical": {
                "compute": shadow_sat_vertical_compute.relative_to(root / "shaders").as_posix(),
            },
            "shadowEvsmBlur": {
                "compute": shadow_evsm_blur_compute.relative_to(root / "shaders").as_posix(),
            },
            "shadowEvsmBlurMoments": {
                "compute": shadow_evsm_blur_moments_compute.relative_to(root / "shaders").as_posix(),
            },
            "shadowCopyDepth": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": shadow_copy_depth_fragment.relative_to(root / "shaders").as_posix(),
            },
            "prepass": {
                "vertex": forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": prepass_fragments,
            },
            "gbuffer": {
                "vertex": forward_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": gbuffer_fragments,
            },
            "gbufferInstanced": {
                "vertex": forward_instanced_vertex.relative_to(root / "shaders").as_posix(),
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
            "deferredLightingAdvanced": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": deferred_lighting_advanced_fragment.relative_to(root / "shaders").as_posix(),
                "bufferFragment": deferred_lighting_advanced_buffer_fragment.relative_to(root / "shaders").as_posix(),
            },
            "deferredLightingAdvancedIms": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": deferred_lighting_advanced_ims_fragment.relative_to(root / "shaders").as_posix(),
                "bufferFragment": deferred_lighting_advanced_ims_buffer_fragment.relative_to(root / "shaders").as_posix(),
            },
            "deferredLightingVariant": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": deferred_variant_fragments,
                "bufferFragments": deferred_variant_buffer_fragments,
            },
            "deferredLightingVariantIms": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragments": deferred_variant_ims_fragments,
                "bufferFragments": deferred_variant_ims_buffer_fragments,
            },
            "litMaterialFlags": {
                "compute": lit_material_flags_compute.relative_to(root / "shaders").as_posix(),
            },
            "litVariantIndirectClear": {
                "compute": lit_variant_indirect_clear_compute.relative_to(root / "shaders").as_posix(),
            },
            "litVariantIndirect": {
                "compute": lit_variant_indirect_compute.relative_to(root / "shaders").as_posix(),
            },
            "deferredLightingVariantCompute": {
                "bufferComputes": deferred_variant_computes,
            },
            "deferredLightingVariantComputeIms": {
                "bufferComputes": deferred_variant_ims_computes,
            },
            "ssao": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssao_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssaoBlur": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssao_blur_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssaoTemporal": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssao_temporal_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssaoUpsample": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssao_upsample_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssrTrace": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssr_trace_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssrTemporal": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssr_temporal_fragment.relative_to(root / "shaders").as_posix(),
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
            "ssgiTemporal": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssgi_temporal_fragment.relative_to(root / "shaders").as_posix(),
            },
            "ssgiComposite": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": ssgi_composite_fragment.relative_to(root / "shaders").as_posix(),
            },
            "motionVectors": {
                "vertex": motion_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": motion_fragment.relative_to(root / "shaders").as_posix(),
            },
            "transparentMotionVectors": {
                "vertex": transparent_motion_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": transparent_motion_fragment.relative_to(root / "shaders").as_posix(),
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
            "depthPyramidCompute": {
                "compute": depth_pyramid_compute.relative_to(root / "shaders").as_posix(),
            },
            "depthPyramidMaxCompute": {
                "compute": depth_pyramid_max_compute.relative_to(root / "shaders").as_posix(),
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
            "debugLightmap": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_lightmap_fragment.relative_to(root / "shaders").as_posix(),
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
            "debugSsgiRaw": {
                "vertex": depth_pyramid_vertex.relative_to(root / "shaders").as_posix(),
                "fragment": debug_ssgi_raw_fragment.relative_to(root / "shaders").as_posix(),
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

    if plan_only:
        try:
            plan = build_plan(passes)
        except ValueError as error:
            print(f"shader plan: {error}", file=sys.stderr)
            return 1
        if args.dump_plan is not None:
            try:
                write_plan(args.dump_plan, plan)
            except OSError as error:
                print(f"shader plan: failed to write {args.dump_plan}: {error}", file=sys.stderr)
                return 1
            print(f"shader plan: wrote {args.dump_plan} ({plan_summary(plan)})")
            return 0
        assert args.plan_check is not None
        try:
            expected = json.loads(args.plan_check.read_text(encoding="utf-8"))
        except OSError as error:
            print(f"shader plan: failed to read {args.plan_check}: {error}", file=sys.stderr)
            return 1
        except json.JSONDecodeError as error:
            print(f"shader plan: invalid JSON in {args.plan_check}: {error}", file=sys.stderr)
            return 1
        difference = compare_plan(expected, plan)
        if difference is not None:
            print(f"shader plan check failed: {args.plan_check}: {difference}", file=sys.stderr)
            print("shader plan: regenerate with "
                  "`py -3 tools/shader_build.py --dump-plan shaders/variants.json`",
                  file=sys.stderr)
            return 1
        print(f"shader plan check: OK ({plan_summary(plan)})")
        return 0

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
