#include "binary.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vfx::detail {
namespace {

constexpr std::uint32_t kHeaderSize = 64u;
constexpr std::uint16_t kBinaryVersion = 6u;
constexpr std::uint32_t kCompilerVersion = 0x00010000u;
constexpr std::uint32_t kFlagsLittleEndian = 1u;
constexpr std::uint32_t kInvalidOffset = 0xffffffffu;
constexpr std::uint32_t kLutSampleCount = 256u;
constexpr std::uint32_t kLutBytes = kLutSampleCount * 8u;

enum SectionId : std::uint32_t {
    kStrings = 1,
    kProperties = 2,
    kLuts = 3,
    kSystems = 4,
    kBuffers = 5,
    kKernels = 6,
    kBindings = 7,
    kOutputs = 8,
    kEvents = 9,
    kBounds = 11,
    kCulling = 12,
};

struct Section {
    std::uint32_t id = 0;
    std::uint32_t alignment = 4;
    std::vector<std::byte> data;
};

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

void appendU8(std::vector<std::byte>& out, std::uint8_t value) {
    out.push_back(static_cast<std::byte>(value));
}

void appendU16(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xffu));
    out.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
}

void appendU32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void appendU64(std::vector<std::byte>& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
    }
}

void patchU32(std::vector<std::byte>& out, std::size_t offset, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out[offset + shift / 8u] = static_cast<std::byte>((value >> shift) & 0xffu);
    }
}

void patchU64(std::vector<std::byte>& out, std::size_t offset, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out[offset + shift / 8u] = static_cast<std::byte>((value >> shift) & 0xffu);
    }
}

std::uint64_t fnv1a64(const std::vector<std::byte>& bytes) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::byte value : bytes) {
        hash ^= static_cast<std::uint8_t>(value);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint16_t floatToHalf(float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16u) & 0x8000u);
    const std::uint32_t exponent = (bits >> 23u) & 0xffu;
    const std::uint32_t mantissa = bits & 0x007fffffu;
    if (exponent == 0xffu) {
        return static_cast<std::uint16_t>(sign | (mantissa != 0u ? 0x7e00u : 0x7c00u));
    }
    int halfExponent = static_cast<int>(exponent) - 127 + 15;
    if (halfExponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    if (halfExponent <= 0) {
        if (halfExponent < -10) {
            return sign;
        }
        const std::uint32_t shifted = (mantissa | 0x00800000u) >> static_cast<unsigned>(14 - halfExponent);
        return static_cast<std::uint16_t>(sign | (shifted & 0x03ffu));
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint16_t>(halfExponent) << 10u) |
                                      ((mantissa >> 13u) & 0x03ffu));
}

void appendHalf(std::vector<std::byte>& out, float value) {
    const std::uint16_t half = floatToHalf(value);
    appendU16(out, half);
}

float sampleCurve(const Property& property, float t) {
    if (property.curve.empty()) {
        return 0.0f;
    }
    t = std::clamp(t, 0.0f, 1.0f);
    if (property.curve.size() == 1 || t <= property.curve.front().time) {
        return property.curve.front().value;
    }
    for (std::size_t i = 1; i < property.curve.size(); ++i) {
        const CurveKey& a = property.curve[i - 1];
        const CurveKey& b = property.curve[i];
        if (t < b.time) {
            const float span = b.time - a.time;
            return span > 0.0f ? a.value + (b.value - a.value) * ((t - a.time) / span) : b.value;
        }
    }
    return property.curve.back().value;
}

Vec4 sampleGradient(const Property& property, float t) {
    if (property.gradient.empty()) {
        return Vec4{};
    }
    t = std::clamp(t, 0.0f, 1.0f);
    if (property.gradient.size() == 1 || t <= property.gradient.front().time) {
        return property.gradient.front().color;
    }
    for (std::size_t i = 1; i < property.gradient.size(); ++i) {
        const GradientKey& a = property.gradient[i - 1];
        const GradientKey& b = property.gradient[i];
        if (t < b.time) {
            const float span = b.time - a.time;
            if (span <= 0.0f) {
                return b.color;
            }
            const float u = (t - a.time) / span;
            return Vec4{
                a.color.x + (b.color.x - a.color.x) * u,
                a.color.y + (b.color.y - a.color.y) * u,
                a.color.z + (b.color.z - a.color.z) * u,
                a.color.w + (b.color.w - a.color.w) * u,
            };
        }
    }
    return property.gradient.back().color;
}

std::uint8_t propertyComponentCount(PropertyType type) {
    switch (type) {
    case PropertyType::Float:
    case PropertyType::Int:
    case PropertyType::Bool:
        return 1;
    case PropertyType::Vector2:
        return 2;
    case PropertyType::Vector3:
        return 3;
    case PropertyType::Vector4:
    case PropertyType::Color:
        return 4;
    case PropertyType::Texture:
    case PropertyType::Curve:
    case PropertyType::Gradient:
        return 0;
    }
    return 0;
}

bool propertyHasLut(PropertyType type) {
    return type == PropertyType::Curve || type == PropertyType::Gradient;
}


struct KernelRecord {
    const SystemDesc* system = nullptr;
    KernelTask task = KernelTask::Spawner;
    ShaderStage stage = ShaderStage::Compute;
    ShaderBackend backend = ShaderBackend::Dxbc;
    SortMode sortMode = SortMode::None;
    std::array<std::uint32_t, 3> groups{1, 1, 1};
    std::uint32_t bindingStart = 0;
    std::uint32_t bindingCount = 0;
    std::uint32_t program = 0;
    std::uint32_t blobOffset = 0;
    const std::vector<std::byte>* blob = nullptr;
};


} // namespace

std::vector<std::byte> writeAssetBinary(const CompiledAsset& asset,
                                        const std::vector<std::vector<BinaryBlob>>& kernelBlobs,
                                        const CompileOptions& options, std::string& error) {
    error.clear();
    if (asset.graph == nullptr) {
        error = "compiled asset has no graph";
        return {};
    }
    std::size_t kernelCount = asset.globalKernels.size();
    for (const CompiledSystem& system : asset.systems) {
        kernelCount += system.kernels.size();
    }
    if (kernelBlobs.size() != kernelCount) {
        error = "compiled kernel blob count mismatch";
        return {};
    }

    std::map<std::string, std::uint32_t> strings;
    strings.emplace("", 0u);
    const auto addString = [&strings](const std::string& value) {
        if (!value.empty()) {
            strings.emplace(value, 0u);
        }
    };
    addString(asset.graph->name);
    for (const Property& property : asset.graph->properties) {
        addString(property.name);
    }
    for (const AttributeDesc& attribute : asset.graph->attributes) {
        addString(attribute.name);
    }
    for (const AttributeDesc& attribute : builtInAttributes()) {
        addString(attribute.name);
    }
    for (const SystemDesc& system : asset.graph->systems) {
        addString(system.name);
    }
    for (const CompiledSystem& system : asset.systems) {
        for (const KernelProgram& kernel : system.kernels) {
            addString(kernel.shader.name);
        }
    }
    for (const KernelProgram& kernel : asset.globalKernels) {
        addString(kernel.shader.name);
    }
    for (const CompiledEventLink& link : asset.eventLinks) {
        addString(link.name);
        for (const EventPayloadAttribute& payload : link.payload) {
            addString(payload.name);
        }
        for (const EventReceiveMapping& receive : link.receives) {
            addString(receive.attribute);
        }
    }
    std::uint32_t stringOffset = 0;
    for (auto& [text, offset] : strings) {
        offset = stringOffset;
        stringOffset += static_cast<std::uint32_t>(text.size() + 1u);
    }

    std::vector<std::byte> stringsData;
    stringsData.reserve(stringOffset);
    for (const auto& [text, offset] : strings) {
        (void)offset;
        for (const char character : text) {
            stringsData.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
        }
        stringsData.push_back(std::byte{0});
    }

    std::vector<std::byte> lutData;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> propertyLuts;
    propertyLuts.reserve(asset.graph->properties.size());
    for (const Property& property : asset.graph->properties) {
        if (!propertyHasLut(property.type)) {
            propertyLuts.emplace_back(kInvalidOffset, 0u);
            continue;
        }
        while (lutData.size() % 16u != 0u) {
            lutData.push_back(std::byte{0});
        }
        const std::uint32_t offset = static_cast<std::uint32_t>(lutData.size());
        for (std::uint32_t i = 0; i < kLutSampleCount; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kLutSampleCount - 1u);
            if (property.type == PropertyType::Curve) {
                const float value = sampleCurve(property, t);
                appendHalf(lutData, value);
                appendHalf(lutData, 0.0f);
                appendHalf(lutData, 0.0f);
                appendHalf(lutData, 0.0f);
            } else {
                const Vec4 value = sampleGradient(property, t);
                appendHalf(lutData, value.x);
                appendHalf(lutData, value.y);
                appendHalf(lutData, value.z);
                appendHalf(lutData, value.w);
            }
        }
        while (lutData.size() % 16u != 0u) {
            lutData.push_back(std::byte{0});
        }
        propertyLuts.emplace_back(offset, kLutBytes);
    }

    std::vector<std::byte> propertiesData;
    appendU32(propertiesData, static_cast<std::uint32_t>(asset.graph->properties.size()));
    for (std::size_t i = 0; i < asset.graph->properties.size(); ++i) {
        const Property& property = asset.graph->properties[i];
        const auto name = strings.find(property.name);
        appendU32(propertiesData, name == strings.end() ? 0u : name->second);
        appendU8(propertiesData, static_cast<std::uint8_t>(property.type));
        appendU8(propertiesData, propertyHasLut(property.type) ? 1u : 0u);
        appendU16(propertiesData, static_cast<std::uint16_t>(propertyComponentCount(property.type)));
        for (float value : property.defaultFloat) {
            appendU32(propertiesData, std::bit_cast<std::uint32_t>(value));
        }
        const auto [lutOffset, lutSize] = propertyLuts[i];
        appendU32(propertiesData, lutOffset);
        appendU32(propertiesData, lutSize);
    }

    std::vector<std::byte> buffersData;
    appendU32(buffersData, static_cast<std::uint32_t>(
                                asset.systems.size() * static_cast<std::uint32_t>(BufferKind::Count)));
    for (std::size_t systemIndex = 0; systemIndex < asset.systems.size(); ++systemIndex) {
        const CompiledSystem& system = asset.systems[systemIndex];
        const std::uint32_t capacity = system.layout.capacity;
        const auto appendBuffer = [&](BufferKind kind, std::uint32_t size, std::uint32_t stride) {
            appendU32(buffersData, static_cast<std::uint32_t>(systemIndex));
            appendU8(buffersData, static_cast<std::uint8_t>(kind));
            appendU8(buffersData, 0u);
            appendU16(buffersData, 0u);
            appendU32(buffersData, size);
            appendU32(buffersData, stride);
            appendU32(buffersData, kInvalidOffset);
            appendU32(buffersData, 0u);
        };
        appendBuffer(BufferKind::Attributes, system.layout.bufferSizeBytes(), system.layout.strideWords * 4u);
        appendBuffer(BufferKind::DeadList, (2u + capacity) * 4u, 4u);
        appendBuffer(BufferKind::Indirection, capacity * 4u, 4u);
        appendBuffer(BufferKind::RenderCount, 4u, 4u);
        appendBuffer(BufferKind::SpawnCount, 4u, 4u);
        appendBuffer(BufferKind::IndirectArgs, 32u, 32u);
        appendBuffer(BufferKind::Bounds, 24u, 4u);
        appendBuffer(BufferKind::RenderKVP, (1u + capacity) * 8u, 8u);
        appendBuffer(BufferKind::SortScratch, capacity * 8u, 8u);
    }

    std::vector<std::byte> systemsData;
    appendU32(systemsData, static_cast<std::uint32_t>(asset.systems.size()));
    std::vector<std::vector<Burst>> systemBursts;
    systemBursts.reserve(asset.systems.size());
    std::uint32_t burstOffset = static_cast<std::uint32_t>(4u + asset.systems.size() * 64u);
    for (const CompiledSystem& system : asset.systems) {
        systemBursts.push_back(system.desc->spawner.bursts);
    }
    std::vector<std::byte> burstData;
    std::vector<std::uint32_t> burstOffsets;
    burstOffsets.reserve(asset.systems.size());
    for (const auto& bursts : systemBursts) {
        burstOffsets.push_back(burstOffset + static_cast<std::uint32_t>(burstData.size()));
        for (const Burst& burst : bursts) {
            appendU32(burstData, std::bit_cast<std::uint32_t>(burst.time));
            appendU32(burstData, burst.count);
            appendU32(burstData, std::bit_cast<std::uint32_t>(burst.period.value_or(0.0f)));
            appendU32(burstData, burst.repeat.value_or(0u));
        }
    }

    std::vector<std::byte> bindingsData;
    std::vector<std::uint32_t> bindingCounts;
    const auto appendKernelBindings = [&](const KernelProgram& kernel) {
        bindingsData.resize(bindingsData.size() + kernel.shader.bindings.size() * 8u);
        std::size_t cursor = bindingsData.size() - kernel.shader.bindings.size() * 8u;
        for (const KernelBinding& binding : kernel.shader.bindings) {
            bindingsData[cursor++] = static_cast<std::byte>(binding.kind);
            bindingsData[cursor++] = static_cast<std::byte>(binding.stage);
            bindingsData[cursor++] = static_cast<std::byte>(binding.access);
            bindingsData[cursor++] = std::byte{0};
            const std::uint32_t index = binding.index;
            for (unsigned shift = 0; shift < 32; shift += 8) {
                bindingsData[cursor++] = static_cast<std::byte>((index >> shift) & 0xffu);
            }
        }
        bindingCounts.push_back(static_cast<std::uint32_t>(kernel.shader.bindings.size()));
    };
    for (const CompiledSystem& system : asset.systems) {
        for (const KernelProgram& kernel : system.kernels) {
            appendKernelBindings(kernel);
        }
    }
    for (const KernelProgram& kernel : asset.globalKernels) {
        appendKernelBindings(kernel);
    }
    std::vector<std::byte> bindingsSection;
    appendU32(bindingsSection, static_cast<std::uint32_t>(bindingsData.size() / 8u));
    bindingsSection.insert(bindingsSection.end(), bindingsData.begin(), bindingsData.end());

    std::vector<std::byte> kernelsSection;
    std::vector<KernelRecord> kernelRecords;
    std::uint32_t bindingStart = 0;
    std::size_t blobSetIndex = 0;
    const auto appendKernelRecords = [&](const KernelProgram& kernel, const SystemDesc* system) {
        const std::vector<BinaryBlob>& blobs = kernelBlobs[blobSetIndex++];
        const std::uint32_t count = bindingCounts[blobSetIndex - 1u];
        for (const BinaryBlob& blob : blobs) {
            KernelRecord record;
            record.system = system;
            record.task = kernel.task;
            record.stage = blob.stage;
            record.backend = blob.backend;
            record.sortMode = kernel.sortMode;
            record.groups = kernel.groups;
            record.bindingStart = bindingStart;
            record.bindingCount = count;
            record.program = kernel.program;
            record.blob = &blob.data;
            kernelRecords.push_back(record);
        }
        bindingStart += count;
    };
    for (const CompiledSystem& system : asset.systems) {
        for (const KernelProgram& kernel : system.kernels) {
            appendKernelRecords(kernel, &*system.desc);
        }
    }
    for (const KernelProgram& kernel : asset.globalKernels) {
        appendKernelRecords(kernel, nullptr);
    }
    const std::uint32_t kernelRecordBytes = static_cast<std::uint32_t>(4u + kernelRecords.size() * 44u);
    const std::uint32_t blobStart = alignUp(kernelRecordBytes, 4u);
    std::uint32_t blobCursor = blobStart;
    for (KernelRecord& record : kernelRecords) {
        record.blobOffset = blobCursor;
        blobCursor += static_cast<std::uint32_t>(alignUp(static_cast<std::uint32_t>(record.blob->size()), 4u));
    }
    appendU32(kernelsSection, static_cast<std::uint32_t>(kernelRecords.size()));
    for (const KernelRecord& record : kernelRecords) {
        appendU32(kernelsSection,
                  record.system == nullptr
                      ? kInvalidOffset
                      : static_cast<std::uint32_t>(record.system - asset.graph->systems.data()));
        appendU8(kernelsSection, static_cast<std::uint8_t>(record.task));
        appendU8(kernelsSection, static_cast<std::uint8_t>(record.stage));
        appendU8(kernelsSection, static_cast<std::uint8_t>(record.backend));
        appendU8(kernelsSection, static_cast<std::uint8_t>(record.sortMode));
        appendU32(kernelsSection, record.groups[0]);
        appendU32(kernelsSection, record.groups[1]);
        appendU32(kernelsSection, record.groups[2]);
        appendU32(kernelsSection, record.bindingStart);
        appendU32(kernelsSection, record.bindingCount);
        appendU32(kernelsSection, record.blobOffset);
        appendU32(kernelsSection, static_cast<std::uint32_t>(record.blob->size()));
        appendU32(kernelsSection, record.program);
        appendU32(kernelsSection, 0u);
    }
    while (kernelsSection.size() < blobStart) {
        kernelsSection.push_back(std::byte{0});
    }
    for (const KernelRecord& record : kernelRecords) {
        kernelsSection.insert(kernelsSection.end(), record.blob->begin(), record.blob->end());
        while (kernelsSection.size() % 4u != 0u) {
            kernelsSection.push_back(std::byte{0});
        }
    }

    std::vector<std::byte> outputsSection;
    std::size_t outputCount = 0;
    for (const CompiledSystem& system : asset.systems) {
        outputCount += system.outputs.size();
    }
    appendU32(outputsSection, static_cast<std::uint32_t>(outputCount));
    for (std::size_t systemIndex = 0; systemIndex < asset.systems.size(); ++systemIndex) {
        for (const OutputProgram& output : asset.systems[systemIndex].outputs) {
            appendU32(outputsSection, static_cast<std::uint32_t>(systemIndex));
            appendU32(outputsSection, output.program);
            appendU8(outputsSection, static_cast<std::uint8_t>(output.blend));
            appendU8(outputsSection, output.lit ? 1u : 0u);
            appendU8(outputsSection, static_cast<std::uint8_t>(output.sortMode));
            appendU8(outputsSection, static_cast<std::uint8_t>(output.sortOrder));
            appendU32(outputsSection, output.textureProperty);
            appendU32(outputsSection, output.textureProperty != kInvalidIndex ? 1u : 0u);
            appendU8(outputsSection, output.softParticles ? 1u : 0u);
            appendU8(outputsSection, output.castShadows ? 1u : 0u);
            appendU8(outputsSection, output.generateMotionVector ? 0u : 1u);
            appendU8(outputsSection, 0u);
            appendU32(outputsSection, std::bit_cast<std::uint32_t>(output.softDistance));
            appendU32(outputsSection, std::bit_cast<std::uint32_t>(output.sortPriority));
        }
    }

    std::size_t firstKernel = 0;
    for (std::size_t systemIndex = 0; systemIndex < asset.systems.size(); ++systemIndex) {
        const CompiledSystem& system = asset.systems[systemIndex];
        const SystemDesc& desc = *system.desc;
        const auto name = strings.find(desc.name);
        appendU32(systemsData, name == strings.end() ? 0u : name->second);
        appendU32(systemsData, desc.requestedCapacity);
        appendU32(systemsData, system.layout.capacity);
        appendU8(systemsData, static_cast<std::uint8_t>(desc.space));
        appendU8(systemsData, static_cast<std::uint8_t>(desc.sort.mode));
        appendU8(systemsData, static_cast<std::uint8_t>(desc.sort.order));
        appendU8(systemsData, static_cast<std::uint8_t>(system.outputs.size()));
        appendU32(systemsData, std::bit_cast<std::uint32_t>(desc.bounds.center.x));
        appendU32(systemsData, std::bit_cast<std::uint32_t>(desc.bounds.center.y));
        appendU32(systemsData, std::bit_cast<std::uint32_t>(desc.bounds.center.z));
        appendU32(systemsData, std::bit_cast<std::uint32_t>(desc.bounds.extents.x));
        appendU32(systemsData, std::bit_cast<std::uint32_t>(desc.bounds.extents.y));
        appendU32(systemsData, std::bit_cast<std::uint32_t>(desc.bounds.extents.z));
        appendU32(systemsData, std::bit_cast<std::uint32_t>(desc.spawner.rate));
        appendU32(systemsData, burstOffsets[systemIndex]);
        appendU32(systemsData, static_cast<std::uint32_t>(systemBursts[systemIndex].size()));
        appendU32(systemsData, static_cast<std::uint32_t>(firstKernel));
        appendU32(systemsData, static_cast<std::uint32_t>(system.kernels.size()));
        appendU8(systemsData, static_cast<std::uint8_t>(desc.bounds.mode));
        appendU8(systemsData, 0u);
        appendU8(systemsData, 0u);
        appendU8(systemsData, 0u);
        firstKernel += system.kernels.size();
    }
    systemsData.insert(systemsData.end(), burstData.begin(), burstData.end());

    std::vector<std::byte> boundsData;
    const bool hasBoundsPadding =
        std::any_of(asset.systems.begin(), asset.systems.end(), [](const CompiledSystem& system) {
            const Vec3& padding = system.desc->bounds.padding;
            return system.desc->bounds.mode == BoundsMode::Automatic &&
                   (padding.x != 0.0f || padding.y != 0.0f || padding.z != 0.0f);
        });
    if (hasBoundsPadding) {
        appendU32(boundsData, static_cast<std::uint32_t>(asset.systems.size()));
        for (const CompiledSystem& system : asset.systems) {
            const Vec3& padding = system.desc->bounds.padding;
            appendU32(boundsData, std::bit_cast<std::uint32_t>(padding.x));
            appendU32(boundsData, std::bit_cast<std::uint32_t>(padding.y));
            appendU32(boundsData, std::bit_cast<std::uint32_t>(padding.z));
        }
    }

    std::vector<std::byte> cullingData;
    const std::uint32_t cullingFlags = asset.graph != nullptr ? asset.graph->cullingFlags : 0u;
    if (cullingFlags != 0u) {
        appendU32(cullingData, cullingFlags);
    }

    std::vector<std::byte> eventsData;
    appendU32(eventsData, static_cast<std::uint32_t>(asset.eventLinks.size()));
    appendU32(eventsData, asset.eventBufferSize);
    for (const CompiledEventLink& link : asset.eventLinks) {
        const auto linkName = strings.find(link.name);
        appendU32(eventsData, link.producerSystem);
        appendU32(eventsData, link.consumerSystem);
        appendU32(eventsData, link.capacity);
        appendU32(eventsData, link.bufferOffsetWords);
        appendU32(eventsData, link.payloadOffsetWords);
        appendU32(eventsData, linkName == strings.end() ? 0u : linkName->second);
        appendU32(eventsData, static_cast<std::uint32_t>(link.payload.size()));
        appendU32(eventsData, static_cast<std::uint32_t>(link.receives.size()));
        for (const EventPayloadAttribute& payload : link.payload) {
            const auto payloadName = strings.find(payload.name);
            appendU32(eventsData, payloadName == strings.end() ? 0u : payloadName->second);
            appendU32(eventsData, payload.slot);
            appendU8(eventsData, static_cast<std::uint8_t>(payload.targetType));
            appendU8(eventsData, 0u);
            appendU16(eventsData, 0u);
            for (const std::uint32_t word : payload.defaultWords) {
                appendU32(eventsData, word);
            }
        }
        for (const EventReceiveMapping& receive : link.receives) {
            const auto receiveName = strings.find(receive.attribute);
            appendU32(eventsData, receiveName == strings.end() ? 0u : receiveName->second);
            appendU32(eventsData, receive.slot);
        }
    }

    std::vector<Section> sections;
    sections.push_back(Section{kStrings, 4, std::move(stringsData)});
    sections.push_back(Section{kProperties, 4, std::move(propertiesData)});
    sections.push_back(Section{kLuts, 16, std::move(lutData)});
    sections.push_back(Section{kSystems, 4, std::move(systemsData)});
    sections.push_back(Section{kBuffers, 4, std::move(buffersData)});
    sections.push_back(Section{kKernels, 4, std::move(kernelsSection)});
    sections.push_back(Section{kBindings, 4, std::move(bindingsSection)});
    sections.push_back(Section{kOutputs, 4, std::move(outputsSection)});
    sections.push_back(Section{kEvents, 4, std::move(eventsData)});
    if (hasBoundsPadding) {
        sections.push_back(Section{kBounds, 4, std::move(boundsData)});
    }
    if (cullingFlags != 0u) {
        sections.push_back(Section{kCulling, 4, std::move(cullingData)});
    }

    std::vector<std::byte> table;
    table.reserve(sections.size() * 16u);
    std::uint32_t offset = kHeaderSize + static_cast<std::uint32_t>(sections.size() * 16u);
    for (const Section& section : sections) {
        offset = alignUp(offset, section.alignment);
        appendU32(table, section.id);
        appendU32(table, offset);
        appendU32(table, static_cast<std::uint32_t>(section.data.size()));
        appendU32(table, section.alignment);
        offset += static_cast<std::uint32_t>(section.data.size());
    }

    std::vector<std::byte> tail;
    tail.reserve(table.size() + offset);
    tail.insert(tail.end(), table.begin(), table.end());
    std::uint32_t contentOffset = kHeaderSize + static_cast<std::uint32_t>(table.size());
    for (const Section& section : sections) {
        contentOffset = alignUp(contentOffset, section.alignment);
        while (tail.size() < contentOffset - kHeaderSize) {
            tail.push_back(std::byte{0});
        }
        tail.insert(tail.end(), section.data.begin(), section.data.end());
        contentOffset += static_cast<std::uint32_t>(section.data.size());
    }
    while (tail.size() < offset - kHeaderSize) {
        tail.push_back(std::byte{0});
    }

    std::vector<std::byte> header;
    header.resize(kHeaderSize, std::byte{0});
    header[0] = static_cast<std::byte>('V');
    header[1] = static_cast<std::byte>('F');
    header[2] = static_cast<std::byte>('X');
    header[3] = static_cast<std::byte>('B');
    patchU32(header, 4, (kHeaderSize << 16u) | static_cast<std::uint32_t>(kBinaryVersion));
    patchU32(header, 8, kFlagsLittleEndian);
    patchU32(header, 12, static_cast<std::uint32_t>(sections.size()));
    patchU64(header, 16, fnv1a64(tail));
    patchU32(header, 24, kCompilerVersion);
    const auto assetName = strings.find(asset.graph->name);
    patchU32(header, 28, assetName == strings.end() ? 0u : assetName->second);
    (void)options;

    std::vector<std::byte> result;
    result.reserve(header.size() + tail.size());
    result.insert(result.end(), header.begin(), header.end());
    result.insert(result.end(), tail.begin(), tail.end());
    return result;
}

} // namespace vfx::detail
