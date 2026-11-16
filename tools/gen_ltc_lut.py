#!/usr/bin/env python3
import argparse
import re
from pathlib import Path


def read_table(source: Path) -> list[int]:
    text = source.read_text(encoding="utf-8-sig", errors="ignore")
    text = re.sub(r"//.*", "", text)
    marker = "s_LtcMatrixData_BRDF_GGX"
    start = text.index(marker)
    start = text.index("{", start)
    end = text.index("}", start)
    values = [int(value) for value in re.findall(r"\b\d+\b", text[start + 1 : end])]
    expected = 64 * 64 * 4
    if len(values) != expected:
        raise ValueError(f"expected {expected} LTC values, found {len(values)}")
    return values


def emit(values: list[int], output: Path) -> None:
    lines = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace hbg::render {",
        "",
        "constexpr uint32_t kLtcGgxLutSize = 64;",
        "constexpr uint32_t kLtcGgxHalfCount = 64 * 64 * 4;",
        "",
        "constexpr uint16_t kLtcGgxHalf[kLtcGgxHalfCount] = {",
    ]
    for index in range(0, len(values), 8):
        chunk = ", ".join(str(value) for value in values[index : index + 8])
        lines.append(f"    {chunk},")
    lines.extend(["};", "", "} // namespace hbg::render", ""])
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    emit(read_table(args.source), args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
