#pragma once

#include "vfx/attribute_layout.h"
#include "vfx/device.h"
#include "vfx/expression.h"
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
    SortBitonic = 8,
    SortPrepass = 9,
    SortMerge = 10,
    EventPrefix = 11,
    EventReset = 12,
    EventCopy = 13,
    BoundsInit = 14,
    Bounds = 15,
    SortReset = 16,
};

enum class BufferKind : std::uint8_t {
    Attributes = 0,
    DeadList = 1,
    Indirection = 2,
    RenderCount = 3,
    SpawnCount = 4,
    IndirectArgs = 5,
    Bounds = 6,
    RenderKVP = 7,
    SortScratch = 8,
    Count = 9,
};

enum class BindingKind : std::uint8_t {
    Buffer = 0,
    Texture = 1,
    Event = 2,
    CpuEvent = 3,
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
    SortMode sortMode = SortMode::None;
    ShaderSource shader;
};

struct OutputProgram {
    const SystemDesc* system = nullptr;
    std::uint32_t program = 0;
    ShaderSource vertex;
    ShaderSource fragment;
    BlendMode blend = BlendMode::Alpha;
    bool lit = false;
    bool softParticles = false;
    bool castShadows = false;
    bool generateMotionVector = true;
    std::int32_t sortPriority = 0;
    float softDistance = 0.1f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    std::string metallicProperty;
    std::string roughnessProperty;
    SortMode sortMode = SortMode::None;
    SortOrder sortOrder = SortOrder::Ascending;
    std::uint32_t textureProperty = kInvalidIndex;
};

struct AttributeUsage {
    std::set<std::string> reads;
    std::set<std::string> writes;
};

struct EventPayloadAttribute {
    std::string name;
    std::uint32_t slot = 0;
    ExprType type = ExprType::Float;
    AttributeType targetType = AttributeType::Float;
    AttributeWords defaultWords{};
};

struct EventReceiveMapping {
    std::string attribute;
    std::uint32_t slot = 0;
};

struct CompiledEventLink {
    std::uint32_t producerSystem = 0;
    std::uint32_t consumerSystem = 0;
    std::string name;
    bool cpu = false;
    std::uint32_t capacity = 0;
    std::uint32_t bufferOffsetWords = 0;
    std::uint32_t payloadOffsetWords = 0;
    std::vector<EventPayloadAttribute> payload;
    std::vector<EventReceiveMapping> receives;
    bool hasTrigger = false;
    std::string triggerCount;
    std::string triggerCondition;
};

struct CompiledSystem {
    const SystemDesc* desc = nullptr;
    AttributeSet attributeSet;
    AttributeLayout layout;
    AttributeUsage usage;
    std::vector<KernelProgram> kernels;
    std::vector<OutputProgram> outputs;
    std::int32_t eventIn = -1;
    std::int32_t eventOut = -1;
};

struct CompiledAsset {
    std::unique_ptr<Graph> graph;
    std::vector<CompiledSystem> systems;
    std::vector<CompiledEventLink> eventLinks;
    std::vector<KernelProgram> globalKernels;
    std::uint32_t eventBufferSize = 0;
    std::string error;
};

struct CompileError {
    std::string path;
    std::string message;

    bool empty() const noexcept { return message.empty(); }
};

CompiledAsset generatePrograms(std::unique_ptr<Graph> graph);

} // namespace vfx::detail
