#pragma once

#include "vfx/attribute_layout.h"
#include "vfx/device.h"
#include "vfx/graph.h"

#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace vfx::detail {

enum class KernelTask : std::uint8_t {
    Spawner = 0,
    DeadListInit = 1,
    Init = 2,
    Update = 3,
    OutputUpdate = 4,
    FillIndirect = 5,
    OutputVertex = 6,
    OutputFragment = 7,
};

enum class BufferKind : std::uint8_t {
    Attributes = 0,
    DeadList = 1,
    Indirection = 2,
    RenderCount = 3,
    SpawnCount = 4,
    IndirectArgs = 5,
    Bounds = 6,
};

enum class BindingKind : std::uint8_t {
    Buffer = 0,
    Texture = 1,
};

struct KernelBinding {
    BindingKind kind = BindingKind::Buffer;
    std::uint8_t stage = 0;
    Access access = Access::Read;
    std::uint32_t index = 0;
};

struct ShaderSource {
    ShaderStage stage = ShaderStage::Compute;
    std::string name;
    std::string source;
    std::string varyings;
    std::vector<KernelBinding> bindings;
};

struct KernelProgram {
    const SystemDesc* system = nullptr;
    KernelTask task = KernelTask::Spawner;
    std::uint32_t program = 0;
    std::array<std::uint32_t, 3> groups{1, 1, 1};
    ShaderSource shader;
};

struct OutputProgram {
    const SystemDesc* system = nullptr;
    std::uint32_t program = 0;
    ShaderSource vertex;
    ShaderSource fragment;
    BlendMode blend = BlendMode::Alpha;
    bool lit = false;
    SortMode sortMode = SortMode::None;
    SortOrder sortOrder = SortOrder::Ascending;
    std::uint32_t textureProperty = kInvalidIndex;
};

struct AttributeUsage {
    std::set<std::string> reads;
    std::set<std::string> writes;
};

struct CompiledSystem {
    const SystemDesc* desc = nullptr;
    AttributeSet attributeSet;
    AttributeLayout layout;
    AttributeUsage usage;
    std::vector<KernelProgram> kernels;
    std::vector<OutputProgram> outputs;
};

struct CompiledAsset {
    std::unique_ptr<Graph> graph;
    std::vector<CompiledSystem> systems;
    std::string error;
};

struct CompileError {
    std::string path;
    std::string message;

    bool empty() const noexcept { return message.empty(); }
};

CompiledAsset generatePrograms(std::unique_ptr<Graph> graph);

} // namespace vfx::detail
