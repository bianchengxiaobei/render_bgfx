#include "runtime_asset.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vfx {
namespace {

constexpr std::uint32_t kHeaderSize = 64u;
constexpr std::uint16_t kBinaryVersion = 6u;
constexpr std::uint32_t kCompilerVersion = 0x00010000u;
constexpr std::uint32_t kFlagsLittleEndian = 1u;
constexpr std::uint32_t kInvalidOffset = 0xffffffffu;

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
    std::span<const std::byte> data;
};

bool fail(std::string& error, std::string message) {
    if (error.empty()) {
        error = std::move(message);
    }
    return false;
}

std::uint8_t readU8(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint8_t>(bytes[offset]);
}

std::uint16_t readU16(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(readU8(bytes, offset)) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(readU8(bytes, offset + 1u)) << 8u);
}

std::uint32_t readU32(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(readU8(bytes, offset + i)) << (i * 8u);
    }
    return value;
}

std::uint64_t readU64(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(readU8(bytes, offset + i)) << (i * 8u);
    }
    return value;
}

float readF32(std::span<const std::byte> bytes, std::size_t offset) {
    return std::bit_cast<float>(readU32(bytes, offset));
}

std::uint64_t fnv1a64(std::span<const std::byte> bytes) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::byte value : bytes) {
        hash ^= static_cast<std::uint8_t>(value);
        hash *= 1099511628211ull;
    }
    return hash;
}

bool isRangeValid(std::span<const std::byte> bytes, std::size_t offset, std::size_t size) {
    return offset <= bytes.size() && size <= bytes.size() - offset;
}

std::string stringAt(std::span<const std::byte> strings, std::uint32_t offset, bool& ok) {
    if (offset >= strings.size()) {
        ok = false;
        return {};
    }
    const std::size_t begin = offset;
    std::size_t end = begin;
    while (end < strings.size() && strings[end] != std::byte{0}) {
        ++end;
    }
    if (end == strings.size()) {
        ok = false;
        return {};
    }
    return std::string(reinterpret_cast<const char*>(strings.data() + begin), end - begin);
}

bool readBindings(std::span<const std::byte> bindings, std::uint32_t start, std::uint32_t count,
                  std::vector<AssetBinding>& out, std::string& error) {
    const std::uint64_t byteStart = 4u + static_cast<std::uint64_t>(start) * 8u;
    const std::uint64_t byteCount = static_cast<std::uint64_t>(count) * 8u;
    if (byteStart + byteCount > bindings.size()) {
        return fail(error, "BINDINGS range is out of bounds");
    }
    out.clear();
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t offset = static_cast<std::size_t>(byteStart) + static_cast<std::size_t>(i) * 8u;
        const std::uint8_t kind = readU8(bindings, offset);
        if (kind > static_cast<std::uint8_t>(AssetBindingKind::CpuEvent)) {
            return fail(error, "BINDINGS has an invalid kind");
        }
        const std::uint8_t access = readU8(bindings, offset + 2u);
        if (access > static_cast<std::uint8_t>(Access::ReadWrite)) {
            return fail(error, "BINDINGS has an invalid access");
        }
        AssetBinding binding;
        binding.kind = static_cast<AssetBindingKind>(kind);
        binding.stage = readU8(bindings, offset + 1u);
        binding.access = static_cast<Access>(access);
        binding.index = readU32(bindings, offset + 4u);
        out.push_back(binding);
    }
    return true;
}

bool readAssetFile(const std::filesystem::path& path, std::vector<std::byte>& out, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return fail(error, "failed to open VFX asset '" + path.string() + "'");
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size < 0) {
        return fail(error, "failed to query VFX asset size '" + path.string() + "'");
    }
    if (size > 512ll * 1024ll * 1024ll) {
        return fail(error, "VFX asset is too large");
    }
    stream.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (!out.empty() && !stream.read(reinterpret_cast<char*>(out.data()), size)) {
        return fail(error, "failed to read VFX asset '" + path.string() + "'");
    }
    return true;
}

bool parseProperties(const Section& sections, std::span<const std::byte> strings, std::span<const std::byte> luts,
                     Asset& asset, std::string& error) {
    if (!isRangeValid(sections.data, 0, 4u)) {
        return fail(error, "PROPERTIES section is truncated");
    }
    const std::uint32_t count = readU32(sections.data, 0);
    if (!isRangeValid(sections.data, 4u, static_cast<std::size_t>(count) * 32u)) {
        return fail(error, "PROPERTIES section is truncated");
    }
    asset.properties.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t offset = 4u + static_cast<std::size_t>(i) * 32u;
        const std::uint32_t nameOffset = readU32(sections.data, offset);
        const std::uint8_t type = readU8(sections.data, offset + 4u);
        if (type > static_cast<std::uint8_t>(PropertyType::Gradient)) {
            return fail(error, "PROPERTIES has an invalid type");
        }
        bool stringOk = true;
        AssetProperty property;
        property.name = stringAt(strings, nameOffset, stringOk);
        if (!stringOk) {
            return fail(error, "PROPERTIES name offset is invalid");
        }
        property.type = static_cast<PropertyType>(type);
        for (unsigned component = 0; component < 4; ++component) {
            property.defaultFloat[component] = readF32(sections.data, offset + 8u + component * 4u);
        }
        const std::uint32_t lutOffset = readU32(sections.data, offset + 24u);
        const std::uint32_t lutSize = readU32(sections.data, offset + 28u);
        if (lutSize != 0u) {
            if (lutOffset == kInvalidOffset || !isRangeValid(luts, lutOffset, lutSize)) {
                return fail(error, "PROPERTIES LUT range is out of bounds");
            }
            const auto* begin = reinterpret_cast<const std::byte*>(luts.data() + lutOffset);
            property.lut.assign(begin, begin + lutSize);
        }
        asset.properties.push_back(std::move(property));
    }
    return true;
}

bool parseSystems(const Section& section, std::span<const std::byte> strings, Asset& asset, std::string& error) {
    if (!isRangeValid(section.data, 0, 4u)) {
        return fail(error, "SYSTEMS section is truncated");
    }
    const std::uint32_t count = readU32(section.data, 0);
    if (!isRangeValid(section.data, 4u, static_cast<std::size_t>(count) * 64u)) {
        return fail(error, "SYSTEMS section is truncated");
    }
    asset.systems.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t offset = 4u + static_cast<std::size_t>(i) * 64u;
        const std::uint32_t nameOffset = readU32(section.data, offset);
        bool stringOk = true;
        AssetSystem system;
        system.name = stringAt(strings, nameOffset, stringOk);
        if (!stringOk) {
            return fail(error, "SYSTEMS name offset is invalid");
        }
        system.requestedCapacity = readU32(section.data, offset + 4u);
        system.capacity = readU32(section.data, offset + 8u);
        const std::uint8_t space = readU8(section.data, offset + 12u);
        const std::uint8_t sortMode = readU8(section.data, offset + 13u);
        const std::uint8_t sortOrder = readU8(section.data, offset + 14u);
        if (space > static_cast<std::uint8_t>(SystemSpace::Local) ||
            sortMode > static_cast<std::uint8_t>(SortMode::YoungestInFront) ||
            sortOrder > static_cast<std::uint8_t>(SortOrder::Descending)) {
            return fail(error, "SYSTEMS has an invalid enum value");
        }
        system.space = static_cast<SystemSpace>(space);
        system.sortMode = static_cast<SortMode>(sortMode);
        system.sortOrder = static_cast<SortOrder>(sortOrder);
        const std::uint8_t boundsMode = readU8(section.data, offset + 60u);
        if (boundsMode > static_cast<std::uint8_t>(BoundsMode::Automatic)) {
            return fail(error, "SYSTEMS has an invalid bounds mode");
        }
        system.boundsMode = static_cast<BoundsMode>(boundsMode);
        const Vec3 center{readF32(section.data, offset + 16u), readF32(section.data, offset + 20u),
                          readF32(section.data, offset + 24u)};
        const Vec3 extents{readF32(section.data, offset + 28u), readF32(section.data, offset + 32u),
                           readF32(section.data, offset + 36u)};
        system.bounds = Bounds{Vec3{center.x - extents.x, center.y - extents.y, center.z - extents.z},
                               Vec3{center.x + extents.x, center.y + extents.y, center.z + extents.z}, true};
        system.spawnerRate = readF32(section.data, offset + 40u);
        const std::uint32_t burstOffset = readU32(section.data, offset + 44u);
        const std::uint32_t burstCount = readU32(section.data, offset + 48u);
        system.firstKernel = readU32(section.data, offset + 52u);
        system.kernelCount = readU32(section.data, offset + 56u);
        if (system.capacity == 0u || system.capacity > kMaxParticleCapacity) {
            return fail(error, "SYSTEMS capacity is out of range");
        }
        if (burstCount != 0u) {
            const std::uint64_t byteOffset = static_cast<std::uint64_t>(burstOffset) +
                                             static_cast<std::uint64_t>(burstCount) * 16u;
            if (byteOffset > section.data.size()) {
                return fail(error, "SYSTEMS burst range is out of bounds");
            }
            system.bursts.reserve(burstCount);
            for (std::uint32_t burstIndex = 0; burstIndex < burstCount; ++burstIndex) {
                const std::size_t burstRecord =
                    static_cast<std::size_t>(burstOffset) + static_cast<std::size_t>(burstIndex) * 16u;
                AssetBurst burst;
                burst.time = readF32(section.data, burstRecord);
                burst.count = readU32(section.data, burstRecord + 4u);
                burst.period = readF32(section.data, burstRecord + 8u);
                burst.repeat = readU32(section.data, burstRecord + 12u);
                if (burst.time < 0.0f || burst.period < 0.0f) {
                    return fail(error, "SYSTEMS burst time/period is invalid");
                }
                system.bursts.push_back(burst);
            }
        }
        asset.systems.push_back(std::move(system));
    }
    return true;
}

bool parseBoundsPadding(const Section& section, Asset& asset, std::string& error) {
    if (!isRangeValid(section.data, 0, 4u)) {
        return fail(error, "BOUNDS section is truncated");
    }
    const std::uint32_t count = readU32(section.data, 0);
    if (static_cast<std::size_t>(count) != asset.systems.size() ||
        !isRangeValid(section.data, 4u, static_cast<std::size_t>(count) * 12u)) {
        return fail(error, "BOUNDS section has an invalid system count");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t offset = 4u + static_cast<std::size_t>(i) * 12u;
        Vec3 padding{readF32(section.data, offset), readF32(section.data, offset + 4u),
                     readF32(section.data, offset + 8u)};
        if (!std::isfinite(padding.x) || !std::isfinite(padding.y) || !std::isfinite(padding.z)) {
            return fail(error, "BOUNDS padding is invalid");
        }
        asset.systems[i].boundsPadding = padding;
    }
    return true;
}

bool parseCulling(const Section& section, Asset& asset, std::string& error) {
    if (section.data.size() != 4u) {
        return fail(error, "CULLING section is truncated");
    }
    const std::uint32_t flags = readU32(section.data, 0);
    if ((flags & ~(kCullSimulation | kCullBoundsUpdate)) != 0u) {
        return fail(error, "CULLING flags are invalid");
    }
    if (flags != 0u &&
        std::any_of(asset.systems.begin(), asset.systems.end(), [](const AssetSystem& system) {
            return system.boundsMode == BoundsMode::Automatic;
        })) {
        return fail(error, "CULLING cannot be combined with automatic bounds");
    }
    asset.cullingFlags = flags;
    return true;
}

bool parseBuffers(const Section& section, Asset& asset, std::string& error) {
    if (!isRangeValid(section.data, 0, 4u)) {
        return fail(error, "BUFFERS section is truncated");
    }
    const std::uint32_t count = readU32(section.data, 0);
    if (!isRangeValid(section.data, 4u, static_cast<std::size_t>(count) * 24u)) {
        return fail(error, "BUFFERS section is truncated");
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t offset = 4u + static_cast<std::size_t>(i) * 24u;
        const std::uint32_t system = readU32(section.data, offset);
        const std::uint8_t kind = readU8(section.data, offset + 4u);
        if (system >= asset.systems.size() || kind > 8u) {
            return fail(error, "BUFFERS has an invalid system/kind");
        }
        AssetBuffer buffer;
        buffer.kind = kind;
        buffer.size = readU32(section.data, offset + 8u);
        buffer.stride = readU32(section.data, offset + 12u);
        if (buffer.size == 0u || buffer.stride == 0u) {
            return fail(error, "BUFFERS has a zero size/stride");
        }
        for (const AssetBuffer& existing : asset.systems[system].buffers) {
            if (existing.kind == buffer.kind) {
                return fail(error, "BUFFERS has a duplicate system kind");
            }
        }
        asset.systems[system].buffers.push_back(buffer);
    }
    for (AssetSystem& system : asset.systems) {
        std::array<bool, 9> seen{};
        for (const AssetBuffer& buffer : system.buffers) {
            seen[buffer.kind] = true;
        }
        for (const bool present : seen) {
            if (!present) {
                return fail(error, "BUFFERS is missing a required system buffer");
            }
        }
    }
    return true;
}

bool parseEvents(const Section& section, std::span<const std::byte> strings, Asset& asset,
                 std::string& error) {
    if (!isRangeValid(section.data, 0, 8u)) {
        return fail(error, "EVENTS section is truncated");
    }
    const std::uint32_t count = readU32(section.data, 0);
    const std::uint32_t bufferSize = readU32(section.data, 4);
    if (count > 128u || bufferSize % 4u != 0u || bufferSize > 256u * 1024u * 1024u) {
        return fail(error, "EVENTS header is invalid");
    }
    if (count == 0u) {
        if (bufferSize != 0u) {
            return fail(error, "EVENTS buffer size is nonzero without links");
        }
        asset.eventBufferSize = 0u;
        return true;
    }
    asset.eventBufferSize = bufferSize;
    const std::uint32_t bufferWords = bufferSize / 4u;
    std::size_t cursor = 8u;
    asset.eventLinks.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!isRangeValid(section.data, cursor, 32u)) {
            return fail(error, "EVENTS link record is truncated");
        }
        AssetEventLink link;
        link.producerSystem = readU32(section.data, cursor);
        link.consumerSystem = readU32(section.data, cursor + 4u);
        link.capacity = readU32(section.data, cursor + 8u);
        link.bufferOffsetWords = readU32(section.data, cursor + 12u);
        link.payloadOffsetWords = readU32(section.data, cursor + 16u);
        const std::uint32_t nameOffset = readU32(section.data, cursor + 20u);
        const std::uint32_t payloadCount = readU32(section.data, cursor + 24u);
        const std::uint32_t receiveCount = readU32(section.data, cursor + 28u);
        cursor += 32u;
        link.cpu = link.producerSystem == kInvalidOffset;
        if ((!link.cpu && link.producerSystem >= asset.systems.size()) ||
            link.consumerSystem >= asset.systems.size() ||
            (!link.cpu && link.producerSystem == link.consumerSystem) ||
            link.capacity == 0u || link.capacity != asset.systems[link.consumerSystem].capacity ||
            payloadCount > 4u || receiveCount > 64u) {
            return fail(error, "EVENTS link metadata is invalid");
        }
        bool stringOk = true;
        link.name = stringAt(strings, nameOffset, stringOk);
        if (!stringOk || link.name.empty()) {
            return fail(error, "EVENTS link name is invalid");
        }
        const std::uint64_t payloadEnd = static_cast<std::uint64_t>(link.payloadOffsetWords) +
                                         static_cast<std::uint64_t>(link.capacity) * 16u;
        if (link.bufferOffsetWords + 5u + link.capacity > link.payloadOffsetWords ||
            payloadEnd > bufferWords) {
            return fail(error, "EVENTS link buffer range is out of bounds");
        }
        if (!isRangeValid(section.data, cursor, static_cast<std::size_t>(payloadCount) * 28u +
                                                static_cast<std::size_t>(receiveCount) * 8u)) {
            return fail(error, "EVENTS link arrays are truncated");
        }
        link.payload.reserve(payloadCount);
        for (std::uint32_t j = 0; j < payloadCount; ++j) {
            AssetEventPayload payload;
            payload.name = stringAt(strings, readU32(section.data, cursor), stringOk);
            payload.slot = readU32(section.data, cursor + 4u);
            const std::uint8_t type = readU8(section.data, cursor + 8u);
            for (unsigned component = 0; component < 4u; ++component) {
                payload.defaultFloat[component] = readF32(section.data, cursor + 12u + component * 4u);
            }
            cursor += 28u;
            if (!stringOk || payload.name.empty() || payload.slot >= 4u ||
                type > static_cast<std::uint8_t>(AttributeType::Bool)) {
                return fail(error, "EVENTS payload metadata is invalid");
            }
            payload.type = static_cast<AttributeType>(type);
            link.payload.push_back(std::move(payload));
        }
        link.receives.reserve(receiveCount);
        for (std::uint32_t j = 0; j < receiveCount; ++j) {
            AssetEventReceive receive;
            receive.attribute = stringAt(strings, readU32(section.data, cursor), stringOk);
            receive.slot = readU32(section.data, cursor + 4u);
            cursor += 8u;
            if (!stringOk || receive.attribute.empty() || receive.slot >= payloadCount) {
                return fail(error, "EVENTS receive metadata is invalid");
            }
            link.receives.push_back(std::move(receive));
        }
        asset.eventLinks.push_back(std::move(link));
    }
    if (cursor > section.data.size()) {
        return fail(error, "EVENTS section is truncated");
    }
    return true;
}

bool parsePrograms(const Section& kernels, const Section& bindings, const Section& outputs, Asset& asset,
                   std::string& error) {
    if (!isRangeValid(kernels.data, 0, 4u)) {
        return fail(error, "KERNELS section is truncated");
    }
    const std::uint32_t kernelCount = readU32(kernels.data, 0);
    if (!isRangeValid(kernels.data, 4u, static_cast<std::size_t>(kernelCount) * 44u)) {
        return fail(error, "KERNELS section is truncated");
    }
    if (!isRangeValid(bindings.data, 0, 4u)) {
        return fail(error, "BINDINGS section is truncated");
    }
    const std::uint32_t bindingCount = readU32(bindings.data, 0);
    if (!isRangeValid(bindings.data, 4u, static_cast<std::size_t>(bindingCount) * 8u)) {
        return fail(error, "BINDINGS section is truncated");
    }

    std::map<std::uint32_t, std::size_t> programMap;
    for (std::uint32_t i = 0; i < kernelCount; ++i) {
        const std::size_t offset = 4u + static_cast<std::size_t>(i) * 44u;
        const std::uint32_t system = readU32(kernels.data, offset);
        const std::uint8_t task = readU8(kernels.data, offset + 4u);
        const std::uint8_t stage = readU8(kernels.data, offset + 5u);
        const std::uint8_t backend = readU8(kernels.data, offset + 6u);
        const std::uint8_t sortMode = readU8(kernels.data, offset + 7u);
        const std::uint32_t program = readU32(kernels.data, offset + 36u);
        const bool global = system == kInvalidOffset;
        if ((!global && system >= asset.systems.size()) || task > 16u || stage > 2u || backend > 1u ||
            sortMode > static_cast<std::uint8_t>(SortMode::YoungestInFront) ||
            (global && task != 11u && task != 12u && task != 13u) ||
            (!global && task >= 11u && task <= 13u)) {
            return fail(error, "KERNELS has an invalid system/task/stage/backend");
        }
        if (!isRangeValid(kernels.data, offset + 8u, 28u)) {
            return fail(error, "KERNELS record is truncated");
        }
        const std::uint32_t bindingStart = readU32(kernels.data, offset + 20u);
        const std::uint32_t recordBindingCount = readU32(kernels.data, offset + 24u);
        const std::uint32_t blobOffset = readU32(kernels.data, offset + 28u);
        const std::uint32_t blobSize = readU32(kernels.data, offset + 32u);
        if (!isRangeValid(kernels.data, blobOffset, blobSize)) {
            return fail(error, "KERNELS blob range is out of bounds");
        }

        std::vector<AssetBinding> recordBindings;
        if (!readBindings(bindings.data, bindingStart, recordBindingCount, recordBindings, error)) {
            return false;
        }

        auto [iterator, inserted] = programMap.emplace(program, asset.programs.size());
        if (inserted) {
            AssetProgram assetProgram;
            assetProgram.program = program;
            assetProgram.system = system;
            assetProgram.task = task;
            assetProgram.output = task == 6u || task == 7u;
            assetProgram.global = global;
            assetProgram.sortMode = static_cast<SortMode>(sortMode);
            assetProgram.groups = {readU32(kernels.data, offset + 8u), readU32(kernels.data, offset + 12u),
                                   readU32(kernels.data, offset + 16u)};
            asset.programs.push_back(std::move(assetProgram));
        }
        AssetProgram& assetProgram = asset.programs[iterator->second];
        if (assetProgram.system != system || assetProgram.global != global ||
            assetProgram.output != (task == 6u || task == 7u) ||
            (!assetProgram.output && assetProgram.task != task)) {
            return fail(error, "KERNELS program metadata is inconsistent");
        }

        const auto* blobBegin = kernels.data.data() + blobOffset;
        std::vector<std::byte> blob(blobBegin, blobBegin + blobSize);
        if (task <= 5u || task >= 8u) {
            if (stage != static_cast<std::uint8_t>(ShaderStage::Compute)) {
                return fail(error, "KERNELS compute program has a non-compute stage");
            }
            if (!assetProgram.computeBlobs[backend].empty()) {
                return fail(error, "KERNELS has a duplicate compute backend");
            }
            if (assetProgram.bindings.empty()) {
                assetProgram.bindings = recordBindings;
            } else if (assetProgram.bindings.size() != recordBindings.size()) {
                return fail(error, "KERNELS compute bindings differ per backend");
            }
            assetProgram.computeBlobs[backend] = std::move(blob);
        } else {
            if (task == 6u && stage != static_cast<std::uint8_t>(ShaderStage::Vertex)) {
                return fail(error, "KERNELS output vertex has a non-vertex stage");
            }
            if (task == 7u && stage != static_cast<std::uint8_t>(ShaderStage::Fragment)) {
                return fail(error, "KERNELS output fragment has a non-fragment stage");
            }
            auto& stageBlobs = task == 6u ? assetProgram.vertexBlobs : assetProgram.fragmentBlobs;
            auto& stageBindings = task == 6u ? assetProgram.vertexBindings : assetProgram.fragmentBindings;
            if (!stageBlobs[backend].empty()) {
                return fail(error, "KERNELS has a duplicate output backend");
            }
            if (stageBindings.empty()) {
                stageBindings = recordBindings;
            } else if (stageBindings.size() != recordBindings.size()) {
                return fail(error, "KERNELS output bindings differ per backend");
            }
            stageBlobs[backend] = std::move(blob);
        }
    }

    if (!isRangeValid(outputs.data, 0, 4u)) {
        return fail(error, "OUTPUTS section is truncated");
    }
    const std::uint32_t outputCount = readU32(outputs.data, 0);
    if (!isRangeValid(outputs.data, 4u, static_cast<std::size_t>(outputCount) * 32u)) {
        return fail(error, "OUTPUTS section is truncated");
    }
    for (std::uint32_t i = 0; i < outputCount; ++i) {
        const std::size_t offset = 4u + static_cast<std::size_t>(i) * 32u;
        const std::uint32_t system = readU32(outputs.data, offset);
        const std::uint32_t program = readU32(outputs.data, offset + 4u);
        const auto iterator = programMap.find(program);
        if (iterator == programMap.end() || iterator->second >= asset.programs.size()) {
            return fail(error, "OUTPUTS references an unknown program");
        }
        AssetProgram& assetProgram = asset.programs[iterator->second];
        if (!assetProgram.output || assetProgram.system != system) {
            return fail(error, "OUTPUTS program metadata is inconsistent");
        }
        const std::uint8_t blend = readU8(outputs.data, offset + 8u);
        const std::uint8_t lit = readU8(outputs.data, offset + 9u);
        const std::uint8_t sortMode = readU8(outputs.data, offset + 10u);
        const std::uint8_t sortOrder = readU8(outputs.data, offset + 11u);
        if (blend > static_cast<std::uint8_t>(BlendMode::Opaque) ||
            sortMode > static_cast<std::uint8_t>(SortMode::YoungestInFront) ||
            sortOrder > static_cast<std::uint8_t>(SortOrder::Descending)) {
            return fail(error, "OUTPUTS has an invalid enum value");
        }
        assetProgram.blend = static_cast<BlendMode>(blend);
        assetProgram.lit = lit != 0u;
        assetProgram.sortMode = static_cast<SortMode>(sortMode);
        assetProgram.sortOrder = static_cast<SortOrder>(sortOrder);
        assetProgram.textureProperty = (readU32(outputs.data, offset + 16u) & 1u) != 0u
                                           ? readU32(outputs.data, offset + 12u)
                                           : kInvalidIndex;
        assetProgram.softParticles = readU8(outputs.data, offset + 20u) != 0u;
        assetProgram.castShadows = readU8(outputs.data, offset + 21u) != 0u;
        assetProgram.generateMotionVector = readU8(outputs.data, offset + 22u) == 0u;
        assetProgram.softDistance = readF32(outputs.data, offset + 24u);
        assetProgram.sortPriority = std::bit_cast<std::int32_t>(readU32(outputs.data, offset + 28u));
        if (!std::isfinite(assetProgram.softDistance) || assetProgram.softDistance < 0.001f) {
            return fail(error, "OUTPUTS has an invalid soft particle distance");
        }
    }

    for (AssetProgram& program : asset.programs) {
        const bool compute = !program.output;
        const bool computeReady = compute && !program.computeBlobs[0].empty() &&
                                  !program.computeBlobs[1].empty();
        const bool outputReady = program.output && !program.vertexBlobs[0].empty() &&
                                 !program.vertexBlobs[1].empty() && !program.fragmentBlobs[0].empty() &&
                                 !program.fragmentBlobs[1].empty();
        if (!computeReady && !outputReady) {
            return fail(error, "KERNELS program is missing a shader backend");
        }
        if (program.output && (program.textureProperty != kInvalidIndex &&
                               program.textureProperty >= asset.properties.size())) {
            return fail(error, "OUTPUTS texture property index is out of bounds");
        }
        if (program.output && program.textureProperty != kInvalidIndex &&
            asset.properties[program.textureProperty].type != PropertyType::Texture) {
            return fail(error, "OUTPUTS texture property is not a texture");
        }
    }
    return true;
}

} // namespace

std::shared_ptr<const Asset> parseAssetBinary(std::span<const std::byte> bytes, std::string& error) {
    error.clear();
    if (bytes.size() < kHeaderSize) {
        fail(error, "VFX asset header is truncated");
        return nullptr;
    }
    if (bytes[0] != std::byte{'V'} || bytes[1] != std::byte{'F'} || bytes[2] != std::byte{'X'} ||
        bytes[3] != std::byte{'B'}) {
        fail(error, "VFX asset magic is invalid");
        return nullptr;
    }
    if (readU16(bytes, 4) != kBinaryVersion || readU16(bytes, 6) != kHeaderSize) {
        fail(error, "VFX asset version/header size is unsupported");
        return nullptr;
    }
    const std::uint32_t flags = readU32(bytes, 8);
    if ((flags & kFlagsLittleEndian) == 0u) {
        fail(error, "VFX asset is not little-endian");
        return nullptr;
    }
    if (readU32(bytes, 24) != kCompilerVersion) {
        fail(error, "VFX asset compiler version is unsupported");
        return nullptr;
    }
    if (readU64(bytes, 16) != fnv1a64(bytes.subspan(kHeaderSize))) {
        fail(error, "VFX asset content hash mismatch");
        return nullptr;
    }

    const std::uint32_t sectionCount = readU32(bytes, 12);
    if (sectionCount < 9u || sectionCount > 64u ||
        !isRangeValid(bytes, kHeaderSize, static_cast<std::size_t>(sectionCount) * 16u)) {
        fail(error, "VFX asset section table is invalid");
        return nullptr;
    }
    std::map<std::uint32_t, Section> sections;
    for (std::uint32_t i = 0; i < sectionCount; ++i) {
        const std::size_t table = kHeaderSize + static_cast<std::size_t>(i) * 16u;
        const std::uint32_t id = readU32(bytes, table);
        const std::uint32_t offset = readU32(bytes, table + 4u);
        const std::uint32_t size = readU32(bytes, table + 8u);
        const std::uint32_t alignment = readU32(bytes, table + 12u);
        if (id == 0u || sections.find(id) != sections.end() || alignment == 0u ||
            (alignment & (alignment - 1u)) != 0u || !isRangeValid(bytes, offset, size)) {
            fail(error, "VFX asset section is invalid");
            return nullptr;
        }
        sections.emplace(id, Section{bytes.subspan(offset, size)});
    }
    const Section* strings = nullptr;
    const Section* properties = nullptr;
    const Section* luts = nullptr;
    const Section* systems = nullptr;
    const Section* buffers = nullptr;
    const Section* kernels = nullptr;
    const Section* bindings = nullptr;
    const Section* outputs = nullptr;
    const Section* events = nullptr;
    const Section* bounds = nullptr;
    const Section* culling = nullptr;
    for (const auto& [id, section] : sections) {
        switch (id) {
        case kStrings:
            strings = &section;
            break;
        case kProperties:
            properties = &section;
            break;
        case kLuts:
            luts = &section;
            break;
        case kSystems:
            systems = &section;
            break;
        case kBuffers:
            buffers = &section;
            break;
        case kKernels:
            kernels = &section;
            break;
        case kBindings:
            bindings = &section;
            break;
        case kOutputs:
            outputs = &section;
            break;
        case kEvents:
            events = &section;
            break;
        case kBounds:
            bounds = &section;
            break;
        case kCulling:
            culling = &section;
            break;
        default:
            break;
        }
    }
    if (strings == nullptr || properties == nullptr || luts == nullptr || systems == nullptr ||
        buffers == nullptr || kernels == nullptr || bindings == nullptr || outputs == nullptr ||
        events == nullptr) {
        fail(error, "VFX asset is missing a required section");
        return nullptr;
    }
    auto asset = std::make_shared<Asset>();
    bool nameOk = true;
    asset->name = stringAt(strings->data, readU32(bytes, 28), nameOk);
    if (!nameOk) {
        fail(error, "VFX asset name offset is invalid");
        return nullptr;
    }
    if (!parseProperties(*properties, strings->data, luts->data, *asset, error) ||
        !parseSystems(*systems, strings->data, *asset, error)) {
        return nullptr;
    }
    if (bounds != nullptr && !parseBoundsPadding(*bounds, *asset, error)) {
        return nullptr;
    }
    if (culling != nullptr && !parseCulling(*culling, *asset, error)) {
        return nullptr;
    }
    if (!parseBuffers(*buffers, *asset, error) ||
        !parseEvents(*events, strings->data, *asset, error) ||
        !parsePrograms(*kernels, *bindings, *outputs, *asset, error)) {
        return nullptr;
    }
    return asset;
}

std::shared_ptr<const Asset> loadAssetBinaryFile(const std::filesystem::path& path, std::string& error) {
    error.clear();
    std::vector<std::byte> bytes;
    if (!readAssetFile(path, bytes, error)) {
        return nullptr;
    }
    return parseAssetBinary(bytes, error);
}

} // namespace vfx
