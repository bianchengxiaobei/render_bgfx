#!/usr/bin/env python3
"""Generate C++ and shader uniform declarations from the HBG IDL."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple

ROOT = Path(__file__).resolve().parent.parent
IDL_DIR = Path("config/uniforms")
CPP_OUTPUT = Path("src/gfx/generated/uniforms.h")
SHADER_OUTPUT = Path("shaders/include/generated/uniforms.sc")
BLOCK_ORDER = {"Global": 0, "Pass": 1, "Material": 2}

TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|[0-9]+|[{}[\];]")
IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


@dataclass(frozen=True)
class TypeInfo:
    cpp: str
    shader: str
    uniform: str
    setter: str
    array_allowed: bool


TYPE_INFO: Dict[str, TypeInfo] = {
    "mat4": TypeInfo("Mat4", "mat4", "Mat4", "set", True),
    "mat3": TypeInfo("Mat3", "mat3", "Mat3", "set", False),
    "vec4": TypeInfo("Vec4", "vec4", "Vec4", "set", True),
    "vec3": TypeInfo("Vec3", "vec4", "Vec4", "setVec3", False),
    "vec2": TypeInfo("Vec2", "vec4", "Vec4", "setVec2", False),
    "float": TypeInfo("float", "vec4", "Vec4", "setScalar", False),
    "int": TypeInfo("std::int32_t", "vec4", "Vec4", "setInt", False),
    "bool": TypeInfo("bool", "vec4", "Vec4", "setBool", False),
}


@dataclass(frozen=True)
class Field:
    name: str
    type_name: str
    count: int

    @property
    def type(self) -> TypeInfo:
        return TYPE_INFO[self.type_name]

    @property
    def cpp_type(self) -> str:
        if self.count > 1:
            return f"std::array<{self.type.cpp}, {self.count}>"
        return self.type.cpp

    @property
    def uniform_type(self) -> str:
        return self.type.uniform


@dataclass(frozen=True)
class Block:
    name: str
    fields: Tuple[Field, ...]
    source: str

    @property
    def prefix(self) -> str:
        return f"u_{self.name.lower()}_"

    def uniform_name(self, field: Field) -> str:
        return f"{self.prefix}{field.name}"


class IdlError(Exception):
    pass


def strip_comments(text: str) -> str:
    lines: List[str] = []
    for line in text.splitlines():
        hash_index = line.find("#")
        slash_index = line.find("//")
        cut = len(line)
        if hash_index >= 0:
            cut = min(cut, hash_index)
        if slash_index >= 0:
            cut = min(cut, slash_index)
        lines.append(line[:cut])
    return "\n".join(lines)


def tokenize(text: str) -> List[str]:
    return TOKEN_RE.findall(strip_comments(text))


def expect(tokens: Sequence[str], index: int, expected: str, source: str) -> int:
    if index >= len(tokens) or tokens[index] != expected:
        raise IdlError(f"{source}: expected '{expected}'")
    return index + 1


def parse_block(tokens: Sequence[str], index: int, source: str) -> Tuple[Block, int]:
    if index >= len(tokens) or tokens[index] != "block":
        raise IdlError(f"{source}: expected 'block'")
    index += 1
    if index >= len(tokens) or not IDENTIFIER_RE.match(tokens[index]):
        raise IdlError(f"{source}: expected block name")
    block_name = tokens[index]
    index += 1
    index = expect(tokens, index, "{", source)

    fields: List[Field] = []
    field_names = set()
    while index < len(tokens) and tokens[index] != "}":
        type_name = tokens[index]
        if type_name not in TYPE_INFO:
            raise IdlError(f"{source}: unsupported type '{type_name}' in block '{block_name}'")
        index += 1

        if index >= len(tokens) or not IDENTIFIER_RE.match(tokens[index]):
            raise IdlError(f"{source}: expected field name in block '{block_name}'")
        field_name = tokens[index]
        if field_name in field_names:
            raise IdlError(f"{source}: duplicate field '{field_name}' in block '{block_name}'")
        field_names.add(field_name)
        index += 1

        count = 1
        if index < len(tokens) and tokens[index] == "[":
            index += 1
            if index >= len(tokens) or not tokens[index].isdigit():
                raise IdlError(f"{source}: expected array size for '{block_name}.{field_name}'")
            count = int(tokens[index])
            if count <= 0:
                raise IdlError(f"{source}: array size must be positive for '{block_name}.{field_name}'")
            index += 1
            index = expect(tokens, index, "]", source)

        if count > 1 and not TYPE_INFO[type_name].array_allowed:
            raise IdlError(
                f"{source}: array of '{type_name}' is not supported for '{block_name}.{field_name}'; "
                "only vec4 and mat4 arrays are allowed"
            )

        index = expect(tokens, index, ";", source)
        fields.append(Field(field_name, type_name, count))

    index = expect(tokens, index, "}", source)
    if not fields:
        raise IdlError(f"{source}: block '{block_name}' is empty")
    return Block(block_name, tuple(fields), source), index


def parse_file(path: Path) -> List[Block]:
    tokens = tokenize(path.read_text(encoding="utf-8"))
    blocks: List[Block] = []
    index = 0
    while index < len(tokens):
        block, index = parse_block(tokens, index, path.name)
        blocks.append(block)
    return blocks


def load_blocks(root: Path) -> List[Block]:
    idl_paths = sorted((root / IDL_DIR).glob("*.idl"))
    if not idl_paths:
        raise IdlError(f"no .idl files found under {root / IDL_DIR}")

    blocks: List[Block] = []
    names = set()
    for path in idl_paths:
        for block in parse_file(path):
            if block.name in names:
                raise IdlError(f"{path.name}: duplicate block '{block.name}'")
            names.add(block.name)
            blocks.append(block)

    blocks.sort(key=lambda block: (BLOCK_ORDER.get(block.name, len(BLOCK_ORDER)), block.name))
    return blocks


def shader_comment(field: Field) -> str:
    if field.type_name == "vec3":
        return " // vec3: .xyz used"
    if field.type_name == "vec2":
        return " // vec2: .xy used"
    if field.type_name == "float":
        return " // float: .x used"
    if field.type_name == "int":
        return " // int: .x used (stored as float)"
    if field.type_name == "bool":
        return " // bool: .x used (0 or 1)"
    return ""


def generate_cpp(blocks: Sequence[Block]) -> str:
    lines: List[str] = [
        "// Generated by tools/idl_gen.py from config/uniforms/*.idl. Do not edit.",
        "#pragma once",
        "",
        '#include "gfx/uniform_block.h"',
        "",
        "#include <array>",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        "namespace hbg::gfx::generated {",
        "",
    ]

    for block_index, block in enumerate(blocks):
        if block_index > 0:
            lines.append("")
        lines.append(f"struct {block.name}Uniforms {{")
        for field in block.fields:
            lines.append(f"    {field.cpp_type} {field.name};")
        lines.append("};")
        lines.append("")

        lines.append(f"class {block.name}UniformBinder {{")
        lines.append("  public:")
        lines.append(f'    static constexpr std::string_view kPrefix = "{block.prefix}";')
        lines.append("")
        lines.append(
            f"    inline static const std::array<UniformDesc, {len(block.fields)}> kDescriptors = {{"
        )
        for field in block.fields:
            lines.append(
                f'        UniformDesc{{"{block.uniform_name(field)}", UniformType::{field.uniform_type}, '
                f"{field.count}}},"
            )
        lines.append("    };")
        lines.append("")
        lines.append(
            "    bool init(ResourceManager& resources) { return m_block.init(resources, kDescriptors, kPrefix); }"
        )
        lines.append("    void shutdown(ResourceManager& resources) { m_block.shutdown(resources); }")
        lines.append("    bool valid() const { return m_block.valid(); }")
        lines.append("    void validateShader(bgfx::ShaderHandle shader) const { m_block.validateShader(shader); }")
        lines.append("")
        lines.append(f"    void upload(const {block.name}Uniforms& uniforms) const {{")
        for index, field in enumerate(block.fields):
            if field.count > 1:
                lines.append(
                    f"        m_block.setArray({index}, uniforms.{field.name}.data(), {field.count});"
                )
            else:
                lines.append(f"        m_block.{field.type.setter}({index}, uniforms.{field.name});")
        lines.append("    }")
        lines.append("")
        lines.append("  private:")
        lines.append("    UniformBlock m_block;")
        lines.append("};")

    lines.append("")
    lines.append("} // namespace hbg::gfx::generated")
    lines.append("")
    return "\n".join(lines)


def generate_shader(blocks: Sequence[Block]) -> str:
    lines: List[str] = [
        "// Generated by tools/idl_gen.py from config/uniforms/*.idl. Do not edit.",
        "#ifndef HBG_GENERATED_UNIFORMS_SC",
        "#define HBG_GENERATED_UNIFORMS_SC",
        "",
    ]

    for block_index, block in enumerate(blocks):
        if block_index > 0:
            lines.append("")
        lines.append(f"// {block.name}")
        for field in block.fields:
            name = block.uniform_name(field)
            suffix = f"[{field.count}]" if field.count > 1 else ""
            lines.append(f"uniform {field.type.shader} {name}{suffix};{shader_comment(field)}")

    lines.append("")
    lines.append("#endif // HBG_GENERATED_UNIFORMS_SC")
    lines.append("")
    return "\n".join(lines)


def write_if_changed(path: Path, content: str, check: bool) -> bool:
    old = path.read_text(encoding="utf-8") if path.exists() else None
    if old == content:
        if not check:
            path.touch()
        return True
    if check:
        print(f"idl_gen: {path} is out of date", file=sys.stderr)
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as file:
        file.write(content)
    print(f"idl_gen: wrote {path}")
    return True


def main(argv: Iterable[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root")
    parser.add_argument("--check", action="store_true", help="verify generated files are up to date")
    args = parser.parse_args(list(argv))

    root = args.root.resolve()
    try:
        blocks = load_blocks(root)
        cpp = generate_cpp(blocks)
        shader = generate_shader(blocks)
    except IdlError as error:
        print(f"idl_gen: {error}", file=sys.stderr)
        return 1

    ok = write_if_changed(root / CPP_OUTPUT, cpp, args.check)
    ok = write_if_changed(root / SHADER_OUTPUT, shader, args.check) and ok
    if not ok:
        print("idl_gen: run 'python tools/idl_gen.py' to regenerate", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
