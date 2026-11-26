#!/usr/bin/env python3
"""Generate docs/shadergraph/nodes-inventory.md from the Unity Shader Graph source.

Usage:
  py -3 tools/sg_node_inventory.py
  py -3 tools/sg_node_inventory.py --unity-root E:/Learn/Graphics/Packages/com.unity.shadergraph
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

DEFAULT_UNITY_ROOT = Path(r"E:/Learn/Graphics/Packages/com.unity.shadergraph")
TITLE_RE = re.compile(r'\[Title\("([^"]*)"\s*,\s*"([^"]*)"(?:\s*,\s*"([^"]*)")?\)\]')
TITLE_EXPR_RE = re.compile(r'\[Title\("([^"]*)"\s*,\s*"([^"]*)"\s*,\s*([A-Za-z_][\w\.]*)\)\]')
CONST_TITLE_RE = re.compile(r'(?:DefaultNodeTitle|const\s+string\s+\w*Title)\s*=\s*"([^"]*)"')
NODE_CLASS_RE = re.compile(r"^\s*(?:internal|public|sealed|abstract)?\s*class\s+\w+Node\s*:", re.MULTILINE)

A_CATEGORY_EXACT = {
    "Math/Basic",
    "Math/Advanced",
    "Math/Range",
    "Math/Round",
    "Math/Interpolation",
    "Input/Basic",
    "Input/Geometry",
    "Input/Property",
    "Channel",
    "Utility/Logic",
}
A_UTILITY_TITLES = {
    "Custom Function",
    "Sub-graph",
    "Keyword",
    "Dropdown",
    "Preview",
}
A_TEXTURE_TITLES = {
    "Sample Texture 2D",
    "Sample Texture 2D LOD",
    "Sampler State",
    "Texture Size",
    "Split Texture Transform",
}
C_PREFIXES = (
    "UI",
    "Input/UI",
    "Terrain",
    "Input/Mesh Deformation",
    "Custom Interpolators",
)


def slugify(text: str) -> str:
    text = text.replace("+", " plus ")
    text = re.sub(r"[^0-9A-Za-z]+", "_", text.strip().lower())
    return text.strip("_") or "node"


def pick_batch(category: str, title: str) -> str:
    if category in A_CATEGORY_EXACT:
        return "A"
    if category == "Input" and title == "Property":
        return "A"
    if category == "Utility" and title in A_UTILITY_TITLES:
        return "A"
    if category == "Input/Texture" and title in A_TEXTURE_TITLES:
        return "A"
    for prefix in C_PREFIXES:
        if category == prefix or category.startswith(prefix + "/"):
            return "C"
    return "B"


def collect(unity_root: Path) -> list[dict[str, str]]:
    nodes_dir = unity_root / "Editor" / "Data" / "Nodes"
    entries: list[dict[str, str]] = []
    for path in sorted(nodes_dir.rglob("*.cs")):
        text = path.read_text(encoding="utf-8", errors="replace")
        title_specs: list[tuple[str, str]] = []
        for match in TITLE_RE.finditer(text):
            parts = [group for group in match.groups() if group]
            title_specs.append(("/".join(parts[:-1]), parts[-1]))
        if not title_specs:
            for match in TITLE_EXPR_RE.finditer(text):
                class_name = match.group(3).split(".")[-1]
                const_match = CONST_TITLE_RE.search(text)
                title = const_match.group(1) if const_match else class_name
                title_specs.append((f"{match.group(1)}/{match.group(2)}", title))
        for category, title in title_specs:
            rel = path.relative_to(unity_root).as_posix()
            entries.append(
                {
                    "category": category,
                    "title": title,
                    "batch": pick_batch(category, title),
                    "unity": rel,
                    "cpp": f"shadergraph/src/nodes/{slugify(category)}/{slugify(title)}.cpp",
                }
            )
    entries.sort(key=lambda item: (item["batch"], item["category"].lower(), item["title"].lower()))
    return entries


def collect_untitled(unity_root: Path) -> list[str]:
    nodes_dir = unity_root / "Editor" / "Data" / "Nodes"
    untitled: list[str] = []
    for path in sorted(nodes_dir.rglob("*.cs")):
        text = path.read_text(encoding="utf-8", errors="replace")
        if TITLE_RE.search(text) or TITLE_EXPR_RE.search(text):
            continue
        if NODE_CLASS_RE.search(text):
            untitled.append(path.relative_to(unity_root).as_posix())
    return untitled


def load_statuses(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("node status file must contain an object")
    return {str(key): str(value).upper() for key, value in data.items()}


def status_key(entry: dict[str, str]) -> str:
    return f"{entry['category']}|{entry['title']}"


def render(
    entries: list[dict[str, str]],
    untitled: list[str],
    unity_root: Path,
    statuses: dict[str, str],
) -> str:
    counts = Counter(entry["batch"] for entry in entries)
    category_counts = Counter(entry["category"] for entry in entries)
    status_counts = Counter(statuses.get(status_key(entry), "TODO") for entry in entries)
    by_batch: dict[str, list[dict[str, str]]] = defaultdict(list)
    for entry in entries:
        by_batch[entry["batch"]].append(entry)

    lines: list[str] = []
    lines.append("# Shader Graph 节点清单（自动生成）\n")
    lines.append("> 由 `tools/sg_node_inventory.py` 从 Unity Shader Graph 源生成，请勿手工编辑。")
    lines.append(f"> 源目录：`{unity_root.as_posix()}`")
    lines.append("> 状态列由实现阶段回填：`TODO` → `IMPL` → `TEST` → `GOLDEN`。")
    lines.append("> 状态映射：`docs/shadergraph/node-status.json`（key 为 `category|title`）。\n")
    lines.append("## 汇总\n")
    lines.append("| 批次 | 节点数 | 说明 |")
    lines.append("|---|---:|---|")
    lines.append(f"| A | {counts.get('A', 0)} | 核心节点，SG-2 核心 + SG-6A 收尾 |")
    lines.append(f"| B | {counts.get('B', 0)} | 常用扩展，SG-6 前半 |")
    lines.append(f"| C | {counts.get('C', 0)} | 长尾/平台节点，SG-6 后半 |")
    lines.append(f"| 合计 | {len(entries)} | 仅统计带 `[Title]` 的节点 |\n")
    lines.append("分类统计：\n")
    lines.append("| 分类 | 节点数 |")
    lines.append("|---|---:|")
    for category, count in sorted(category_counts.items(), key=lambda item: (-item[1], item[0].lower())):
        lines.append(f"| `{category}` | {count} |")
    lines.append("")
    lines.append("状态统计：\n")
    lines.append("| 状态 | 节点数 |")
    lines.append("|---|---:|")
    for status in ("TEST", "IMPL", "GOLDEN", "TODO"):
        lines.append(f"| {status} | {status_counts.get(status, 0)} |")
    lines.append("")

    for batch in ("A", "B", "C"):
        items = by_batch.get(batch, [])
        lines.append(f"## 批次 {batch}（{len(items)}）\n")
        lines.append("| 状态 | 分类 | 节点 | Unity 源 | 计划 C++ |")
        lines.append("|---|---|---|---|---|")
        for item in items:
            status = statuses.get(status_key(item), "TODO")
            lines.append(
                f"| {status} | `{item['category']}` | {item['title']} | `{item['unity']}` | `{item['cpp']}` |"
            )
        lines.append("")

    lines.append("## 无 `[Title]` 的节点类/基础设施\n")
    lines.append("以下文件包含 `class *Node` 但未声明 `[Title]`，需要实现或明确排除（BlockNode、RedirectNode、SubGraphOutputNode、LegacyUnknownTypeNode 等）：\n")
    for rel in untitled:
        lines.append(f"- `{rel}`")
    lines.append("")

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--unity-root", type=Path, default=DEFAULT_UNITY_ROOT)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "docs" / "shadergraph" / "nodes-inventory.md",
    )
    parser.add_argument(
        "--status",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "docs" / "shadergraph" / "node-status.json",
    )
    args = parser.parse_args()

    entries = collect(args.unity_root)
    untitled = collect_untitled(args.unity_root)
    statuses = load_statuses(args.status)
    text = render(entries, untitled, args.unity_root, statuses)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8")
    print(f"wrote {args.output} ({len(entries)} titled nodes, {len(untitled)} untitled node files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
