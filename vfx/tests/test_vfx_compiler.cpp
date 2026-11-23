#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "binary.h"
#include "codegen.h"
#include "runtime_asset.h"
#include "vfx/asset.h"
#include "vfx/compiler.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t kHeaderSize = 64u;

std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

std::uint16_t readU16(const std::vector<std::byte>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8u);
}

float halfToFloat(std::uint16_t half) {
    const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000u) << 16u;
    const std::uint32_t exponent = (half >> 10u) & 0x1fu;
    const std::uint32_t mantissa = half & 0x03ffu;
    if (exponent == 0u) {
        if (mantissa == 0u) {
            return 0.0f;
        }
        const float value = std::ldexp(static_cast<float>(mantissa), -24);
        return sign != 0u ? -value : value;
    }
    if (exponent == 31u) {
        return std::bit_cast<float>(sign | 0x7f800000u | (mantissa << 13u));
    }
    return std::bit_cast<float>(sign | ((exponent + 112u) << 23u) | (mantissa << 13u));
}

std::uint32_t readU32(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[offset + i]) << (i * 8u);
    }
    return value;
}

std::uint64_t readU64(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8u);
    }
    return value;
}

std::uint64_t fnv1a64(const std::vector<std::byte>& bytes, std::size_t begin) {
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t i = begin; i < bytes.size(); ++i) {
        hash ^= static_cast<std::uint8_t>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<std::byte> makeBlob(std::string_view bytes) {
    std::vector<std::byte> result(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        result[i] = static_cast<std::byte>(static_cast<unsigned char>(bytes[i]));
    }
    return result;
}

std::vector<std::vector<vfx::detail::BinaryBlob>> makeBlobs(
    const vfx::detail::CompiledAsset& asset) {
    std::vector<std::vector<vfx::detail::BinaryBlob>> result;
    const auto appendBlobs = [&](const vfx::detail::KernelProgram& kernel) {
        std::vector<vfx::detail::BinaryBlob> blobs;
        for (const vfx::ShaderBackend backend : {vfx::ShaderBackend::Dxbc, vfx::ShaderBackend::Spirv}) {
            vfx::detail::BinaryBlob blob;
            blob.stage = kernel.shader.stage;
            blob.backend = backend;
            blob.data = backend == vfx::ShaderBackend::Dxbc ? makeBlob("DXBC-test")
                                                             : makeBlob("\x03\x02\x23\x07");
            blobs.push_back(std::move(blob));
        }
        result.push_back(std::move(blobs));
    };
    for (const vfx::detail::CompiledSystem& system : asset.systems) {
        for (const vfx::detail::KernelProgram& kernel : system.kernels) {
            appendBlobs(kernel);
        }
    }
    for (const vfx::detail::KernelProgram& kernel : asset.globalKernels) {
        appendBlobs(kernel);
    }
    return result;
}

std::filesystem::path sourcePath() {
#ifdef VFX_SOURCE_DIR
    return std::filesystem::path(VFX_SOURCE_DIR);
#else
    return std::filesystem::path(".");
#endif
}

std::string environmentValue(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

} // namespace

TEST_CASE("vfx compiler emits GPU event kernels and binary metadata") {
    vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/vfx_event.vfx.json");
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    REQUIRE(asset.error.empty());
    REQUIRE(asset.systems.size() == 2u);
    REQUIRE(asset.eventLinks.size() == 1u);
    CHECK(asset.systems[0].eventOut == 0);
    CHECK(asset.systems[1].eventIn == 0);
    CHECK(asset.eventLinks[0].producerSystem == 0u);
    CHECK(asset.eventLinks[0].consumerSystem == 1u);
    CHECK(asset.eventLinks[0].capacity == 64u);
    CHECK(asset.eventLinks[0].payload.size() == 2u);
    CHECK(asset.eventLinks[0].receives.size() == 2u);
    CHECK(asset.eventBufferSize == (5u + 64u + 64u * 16u) * 4u);
    REQUIRE(asset.globalKernels.size() == 2u);
    CHECK(asset.globalKernels[0].task == vfx::detail::KernelTask::EventReset);
    CHECK(asset.globalKernels[1].task == vfx::detail::KernelTask::EventPrefix);
    CHECK(asset.globalKernels[1].shader.source.find("vfxEventPrefix") != std::string::npos);

    const auto findKernel = [](const vfx::detail::CompiledSystem& system,
                               vfx::detail::KernelTask task) -> const vfx::detail::KernelProgram* {
        for (const vfx::detail::KernelProgram& kernel : system.kernels) {
            if (kernel.task == task) {
                return &kernel;
            }
        }
        return nullptr;
    };
    const auto* update = findKernel(asset.systems[0], vfx::detail::KernelTask::Update);
    const auto* init = findKernel(asset.systems[1], vfx::detail::KernelTask::Init);
    REQUIRE(update != nullptr);
    REQUIRE(init != nullptr);
    CHECK(update->shader.source.find("s_vfxEvents") != std::string::npos);
    CHECK(update->shader.source.find("vfxWasAlive") != std::string::npos);
    CHECK(update->shader.source.find("vfxEventCount") != std::string::npos);
    CHECK(init->shader.source.find("eventCount") != std::string::npos);
    CHECK(init->shader.source.find("eventBase") != std::string::npos);
    CHECK(init->shader.source.find("vfxEventValue") != std::string::npos);

    const auto blobs = makeBlobs(asset);
    vfx::CompileOptions options;
    std::string error;
    const std::vector<std::byte> binary =
        vfx::detail::writeAssetBinary(asset, blobs, options, error);
    REQUIRE(error.empty());
    std::string parseError;
    const std::shared_ptr<const vfx::Asset> decoded = vfx::parseAssetBinary(binary, parseError);
    INFO(parseError);
    REQUIRE(decoded != nullptr);
    REQUIRE(decoded->eventLinks.size() == 1u);
    CHECK(decoded->eventBufferSize == asset.eventBufferSize);
    CHECK(decoded->eventLinks[0].bufferOffsetWords == 0u);
    CHECK(decoded->eventLinks[0].payload.size() == 2u);
    CHECK(decoded->eventLinks[0].receives.size() == 2u);
    bool sawReset = false;
    bool sawPrefix = false;
    for (const vfx::AssetProgram& program : decoded->programs) {
        if (program.global && program.task == 12u) {
            sawReset = true;
        }
        if (program.global && program.task == 11u) {
            sawPrefix = true;
        }
    }
    CHECK(sawReset);
    CHECK(sawPrefix);
}

TEST_CASE("vfx compiler emits CPU event metadata and copy program") {
    vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/vfx_cpu_event.vfx.json");
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    INFO(asset.error);
    REQUIRE(asset.error.empty());
    REQUIRE(asset.systems.size() == 1u);
    REQUIRE(asset.eventLinks.size() == 1u);
    const vfx::detail::CompiledEventLink& link = asset.eventLinks[0];
    CHECK(link.cpu);
    CHECK(link.producerSystem == 0xffffffffu);
    CHECK(link.consumerSystem == 0u);
    CHECK(link.payload.size() == 2u);
    CHECK(link.receives.size() == 2u);
    REQUIRE(asset.globalKernels.size() == 3u);
    CHECK(asset.globalKernels[0].task == vfx::detail::KernelTask::EventCopy);
    CHECK(asset.globalKernels[1].task == vfx::detail::KernelTask::EventReset);
    CHECK(asset.globalKernels[2].task == vfx::detail::KernelTask::EventPrefix);
    CHECK(asset.globalKernels[0].shader.source.find("s_vfxCpuEvents") != std::string::npos);

    const auto blobs = makeBlobs(asset);
    vfx::CompileOptions options;
    std::string error;
    const std::vector<std::byte> binary =
        vfx::detail::writeAssetBinary(asset, blobs, options, error);
    REQUIRE(error.empty());
    std::string parseError;
    const std::shared_ptr<const vfx::Asset> decoded = vfx::parseAssetBinary(binary, parseError);
    INFO(parseError);
    REQUIRE(decoded != nullptr);
    REQUIRE(decoded->eventLinks.size() == 1u);
    CHECK(decoded->eventLinks[0].cpu);
    CHECK(decoded->eventLinks[0].producerSystem == 0xffffffffu);
    REQUIRE(decoded->eventLinks[0].payload.size() == 2u);
    for (const vfx::AssetEventPayload& payload : decoded->eventLinks[0].payload) {
        CHECK(static_cast<int>(payload.type) == static_cast<int>(vfx::AttributeType::Float3));
        CHECK(payload.slot < 4u);
    }
}

TEST_CASE("vfx compiler compiles trigger_event count and validates receivers") {
    const std::string json = R"json({
      "version": 1, "name": "TriggerEvent",
      "systems": [
        {"name": "A", "capacity": 16, "space": "world",
         "spawner": {"rate": 0, "bursts": [{"time": 0.0, "count": 4}]},
         "init": {"blocks": [{"type": "set_position_box", "center": [1,2,3], "size": [0,0,0]}]},
         "update": {"blocks": [{"type": "age"}, {"type": "trigger_event", "event": "born", "count": 2}]},
         "events": {"emit": [{"name": "born", "attributes": {"position": {"op": "attr", "name": "position"}}}]},
         "outputs": [{}]},
        {"name": "B", "capacity": 16,
         "spawner": {"rate": 0, "bursts": []},
         "update": {"blocks": [{"type": "age"}]},
         "events": {"receive": [{"name": "born", "attributes": {"position": "position"}}]},
         "outputs": [{}]}
      ]
    })json";
    vfx::ParseResult parsed = vfx::parseGraphJson(json);
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    INFO(asset.error);
    REQUIRE(asset.error.empty());
    REQUIRE(asset.systems.size() == 2u);
    const auto& update = asset.systems[0].kernels[3].shader.source;
    CHECK(update.find("max(2.0, 0.0)") != std::string::npos);

    auto invalid = vfx::parseGraphJson(json);
    REQUIRE(invalid.ok());
    vfx::Value missing;
    missing.kind = vfx::Value::Kind::String;
    missing.string = "missing";
    invalid.graph->systems[1].events.receive[0].attributes.object[0].second = missing;
    vfx::detail::CompiledAsset invalidAsset = vfx::detail::generatePrograms(std::move(invalid.graph));
    CHECK(invalidAsset.error.find("unknown emit attribute") != std::string::npos);
}

TEST_CASE("vfx compiler generates the minimal kernel plan") {
    vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/minimal_unlit.vfx.json");
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    REQUIRE(asset.error.empty());
    REQUIRE(asset.graph != nullptr);
    REQUIRE(asset.systems.size() == 1u);
    const vfx::detail::CompiledSystem& system = asset.systems.front();
    CHECK(system.layout.capacity == 64u);
    CHECK(system.layout.strideWords == 27u);
    CHECK(system.usage.writes.count("position") == 1u);
    CHECK(system.usage.writes.count("velocity") == 1u);
    CHECK(system.usage.writes.count("color") == 1u);
    CHECK(system.usage.writes.count("age") == 1u);
    CHECK(system.usage.writes.count("alive") == 1u);
    CHECK(system.kernels.size() == 8u);
    CHECK(system.outputs.size() == 1u);
    CHECK(system.kernels[0].task == vfx::detail::KernelTask::Spawner);
    CHECK(system.kernels[1].task == vfx::detail::KernelTask::DeadListInit);
    CHECK(system.kernels[2].task == vfx::detail::KernelTask::Init);
    CHECK(system.kernels[3].task == vfx::detail::KernelTask::Update);
    CHECK(system.kernels[4].task == vfx::detail::KernelTask::OutputUpdate);
    CHECK(system.kernels[5].task == vfx::detail::KernelTask::FillIndirect);
    CHECK(system.kernels[6].task == vfx::detail::KernelTask::OutputVertex);
    CHECK(system.kernels[7].task == vfx::detail::KernelTask::OutputFragment);
    CHECK(system.kernels[6].shader.source.find("% 6u") != std::string::npos);
    CHECK(system.kernels[2].shader.source.find("NUM_THREADS(64, 1, 1)") != std::string::npos);
    CHECK(system.kernels[2].shader.source.find("set_position_box") == std::string::npos);
    CHECK(system.kernels[2].shader.source.find("a_position") != std::string::npos);
    CHECK(system.kernels[3].shader.source.find("a_age = a_age + VFX_DT") != std::string::npos);
    CHECK(system.kernels[3].shader.source.find("a_alive = false") != std::string::npos);
    CHECK(system.kernels[0].shader.bindings.size() == 3u);
    CHECK(system.kernels[0].shader.source.find("uint(u_vfx_params.z + 0.5)") != std::string::npos);
    CHECK(system.kernels[2].shader.source.find("rawLoadUint(s_vfxSpawnCount, 0u)") != std::string::npos);
    CHECK(system.kernels[2].shader.bindings.size() == 3u);
    CHECK(system.kernels[4].shader.bindings.size() == 4u);
    CHECK(system.kernels[4].shader.source.find("s_vfxRenderKVP") != std::string::npos);
    CHECK(system.kernels[4].shader.source.find("kvpOffset") != std::string::npos);
    CHECK(system.kernels[5].shader.bindings.size() == 3u);
    CHECK(system.kernels[5].shader.source.find("s_vfxRenderKVP") != std::string::npos);
    CHECK(system.kernels[6].shader.bindings[0].stage == 12u);
    CHECK(system.kernels[6].shader.bindings[1].stage == 13u);
}

TEST_CASE("vfx compiler generates force and turbulence update code") {
    vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/forces_1024.vfx.json");
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    REQUIRE(asset.error.empty());
    REQUIRE(asset.systems.size() == 1u);
    const std::string& update = asset.systems[0].kernels[3].shader.source;
    CHECK(update.find("vfxCurlNoise3D") != std::string::npos);
    CHECK(update.find("vfxValueNoise2D") != std::string::npos);
    CHECK(update.find("drag *= a_size * a_size * a_scale.x * a_scale.y") != std::string::npos);
    CHECK(update.find("(forceValue - a_velocity) * min(1.0") != std::string::npos);
    CHECK(update.find("(fieldValue - a_velocity) * min(1.0") != std::string::npos);
    CHECK(update.find("fieldPosition") != std::string::npos);
}

TEST_CASE("vfx compiler generates plane and sphere collision code") {
    vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/collision_1024.vfx.json");
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    REQUIRE(asset.error.empty());
    REQUIRE(asset.systems.size() == 1u);
    const std::string& update = asset.systems[0].kernels[3].shader.source;
    CHECK(update.find("const vec3 collisionNormalRaw") != std::string::npos);
    CHECK(update.find("collisionDistanceA") != std::string::npos);
    CHECK(update.find("collisionDiscriminant") != std::string::npos);
    CHECK(update.find("(1.0 + collisionRestitution)") != std::string::npos);
    CHECK(update.find("a_position = collisionHitPos") != std::string::npos);
}

TEST_CASE("vfx compiler supports collision kill and stick modes") {
    const std::string json = R"json({
      "version": 1, "name": "CollisionModes", "properties": [],
      "systems": [{"name": "Main", "capacity": 64,
        "update": {"blocks": [
          {"type": "collision_plane", "normal": [0, 1, 0], "distance": 0.0, "mode": "kill"},
          {"type": "collision_sphere", "center": [0, 0, 0], "radius": 1.0, "mode": "stick"}
        ]},
        "outputs": [{}]}]}
    )json";
    vfx::ParseResult parsed = vfx::parseGraphJson(json);
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    REQUIRE(asset.error.empty());
    const std::string& update = asset.systems[0].kernels[3].shader.source;
    CHECK(update.find("a_alive = false") != std::string::npos);
    CHECK(update.find("a_velocity = vec3(0.0, 0.0, 0.0)") != std::string::npos);
}

TEST_CASE("vfx compiler supports property expressions in turbulence") {
    const std::string json = R"json({
      "version": 1, "name": "TurbulenceExpressions",
      "properties": [{"name": "TurbulenceIntensity", "type": "float", "default": 1.0}],
      "systems": [{"name": "Main", "capacity": 64,
        "spawner": {"rate": 0, "bursts": [{"time": 0.0, "count": 1}]},
        "update": {"blocks": [
          {"type": "turbulence", "intensity": {"op": "property", "name": "TurbulenceIntensity"}}
        ]},
        "outputs": [{}]}]
    })json";
    vfx::ParseResult parsed = vfx::parseGraphJson(json);
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    REQUIRE(asset.error.empty());
    REQUIRE(asset.systems.size() == 1u);
    const std::string& update = asset.systems[0].kernels[3].shader.source;
    CHECK(update.find("u_vfxProp_TurbulenceIntensity") != std::string::npos);
    CHECK(update.find("vfxCurlNoise3D") != std::string::npos);
}

TEST_CASE("vfx binary writer emits deterministic sections and hashes") {
    vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/minimal_unlit.vfx.json");
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    REQUIRE(asset.error.empty());
    const auto blobs = makeBlobs(asset);
    vfx::CompileOptions options;
    std::string error;
    const std::vector<std::byte> binary = vfx::detail::writeAssetBinary(asset, blobs, options, error);
    REQUIRE(error.empty());
    REQUIRE(binary.size() >= kHeaderSize);
    CHECK(binary[0] == std::byte{'V'});
    CHECK(binary[1] == std::byte{'F'});
    CHECK(binary[2] == std::byte{'X'});
    CHECK(binary[3] == std::byte{'B'});
    CHECK(readU16(binary, 4) == 6u);
    CHECK(readU16(binary, 6) == kHeaderSize);
    CHECK(readU32(binary, 8) == 1u);
    const std::uint32_t sectionCount = readU32(binary, 12);
    CHECK(sectionCount == 9u);
    CHECK(readU64(binary, 16) == fnv1a64(binary, kHeaderSize));
    CHECK(readU32(binary, 24) == 0x00010000u);

    const auto section = [&](std::uint32_t id) {
        for (std::uint32_t i = 0; i < sectionCount; ++i) {
            const std::size_t offset = kHeaderSize + i * 16u;
            if (readU32(binary, offset) == id) {
                return std::array<std::uint32_t, 2>{readU32(binary, offset + 4u), readU32(binary, offset + 8u)};
            }
        }
        return std::array<std::uint32_t, 2>{0u, 0u};
    };

    const auto [stringsOffset, stringsSize] = section(1);
    const std::string strings(reinterpret_cast<const char*>(binary.data() + stringsOffset), stringsSize);
    CHECK(strings.find("MinimalUnlit") != std::string::npos);
    CHECK(strings.find("Sparks") != std::string::npos);
    CHECK(strings.find("Tint") != std::string::npos);

    const auto [propertiesOffset, propertiesSize] = section(2);
    CHECK(readU32(binary, propertiesOffset) == 2u);
    CHECK(propertiesSize == 4u + 2u * 32u);

    const auto [systemsOffset, systemsSize] = section(4);
    CHECK(readU32(binary, systemsOffset) == 1u);
    CHECK(systemsSize == 4u + 64u + 16u);

    const auto [buffersOffset, buffersSize] = section(5);
    CHECK(readU32(binary, buffersOffset) == 9u);
    CHECK(buffersSize == 4u + 9u * 24u);

    const auto [kernelsOffset, kernelsSize] = section(6);
    CHECK(readU32(binary, kernelsOffset) == 16u);
    CHECK(kernelsSize > 16u * 44u);

    const auto [bindingsOffset, bindingsSize] = section(7);
    CHECK(readU32(binary, bindingsOffset) > 0u);
    CHECK(bindingsSize >= 4u + readU32(binary, bindingsOffset) * 8u);

    const auto [outputsOffset, outputsSize] = section(8);
    CHECK(readU32(binary, outputsOffset) == 1u);
    CHECK(outputsSize == 4u + 32u);

    const auto [eventsOffset, eventsSize] = section(9);
    CHECK(readU32(binary, eventsOffset) == 0u);
    CHECK(eventsSize == 8u);

    const std::vector<std::byte> again = vfx::detail::writeAssetBinary(asset, blobs, options, error);
    CHECK(error.empty());
    CHECK(again == binary);
    const std::vector<std::byte> dxbcMagic = makeBlob("DXBC-test");
    CHECK(std::search(binary.begin(), binary.end(), dxbcMagic.begin(), dxbcMagic.end()) != binary.end());
}

TEST_CASE("vfx binary asset roundtrips through the reader") {
    vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/minimal_unlit.vfx.json");
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset compiled = vfx::detail::generatePrograms(std::move(parsed.graph));
    REQUIRE(compiled.error.empty());
    const auto blobs = makeBlobs(compiled);
    vfx::CompileOptions options;
    std::string error;
    const std::vector<std::byte> binary = vfx::detail::writeAssetBinary(compiled, blobs, options, error);
    REQUIRE(error.empty());

    std::string parseError;
    const std::shared_ptr<const vfx::Asset> asset = vfx::parseAssetBinary(binary, parseError);
    INFO(parseError);
    REQUIRE(asset != nullptr);
    CHECK(asset->name == "MinimalUnlit");
    REQUIRE(asset->properties.size() == 2u);
    CHECK(asset->properties[0].name == "Tint");
    CHECK(static_cast<int>(asset->properties[0].type) == static_cast<int>(vfx::PropertyType::Color));
    CHECK(asset->properties[1].name == "Size");
    CHECK(static_cast<int>(asset->properties[1].type) == static_cast<int>(vfx::PropertyType::Float));
    REQUIRE(asset->systems.size() == 1u);
    CHECK(asset->systems[0].name == "Sparks");
    CHECK(asset->systems[0].requestedCapacity == 64u);
    CHECK(asset->systems[0].capacity == 64u);
    CHECK(asset->systems[0].spawnerRate == doctest::Approx(40.0f));
    REQUIRE(asset->systems[0].bursts.size() == 1u);
    CHECK(asset->systems[0].bursts[0].count == 8u);
    CHECK(asset->systems[0].buffers.size() == 9u);
    REQUIRE(asset->systems[0].buffers[7].kind == 7u);
    CHECK(asset->systems[0].buffers[7].size == (64u + 1u) * 8u);
    CHECK(asset->systems[0].buffers[7].stride == 8u);
    REQUIRE(asset->systems[0].buffers[8].kind == 8u);
    CHECK(asset->systems[0].buffers[8].size == 64u * 8u);
    CHECK(asset->systems[0].buffers[8].stride == 8u);
    CHECK(asset->systems[0].kernelCount == 8u);
    REQUIRE(asset->programs.size() == 7u);
    CHECK(asset->programs[0].task == 0u);
    CHECK(asset->programs[6].output);
    CHECK(static_cast<int>(asset->programs[6].blend) == static_cast<int>(vfx::BlendMode::Additive));
    for (const vfx::AssetBuffer& buffer : asset->systems[0].buffers) {
        CHECK(buffer.size > 0u);
        CHECK(buffer.stride > 0u);
    }
    const std::shared_ptr<const vfx::Asset> second = vfx::parseAssetBinary(binary, parseError);
    REQUIRE(second != nullptr);
    CHECK(second->name == asset->name);
    CHECK(second->properties.size() == asset->properties.size());
    CHECK(second->systems.size() == asset->systems.size());
    CHECK(second->programs.size() == asset->programs.size());
}

TEST_CASE("vfx compiler bakes LUTs and binds curve texture stages") {
    vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/curve_unlit.vfx.json");
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset compiled = vfx::detail::generatePrograms(std::move(parsed.graph));
    REQUIRE(compiled.error.empty());
    const vfx::detail::CompiledSystem& system = compiled.systems.front();
    REQUIRE(system.kernels.size() >= 4u);
    const std::string& initSource = system.kernels[2].shader.source;
    const std::string& updateSource = system.kernels[3].shader.source;
    CHECK(initSource.find("SAMPLER2D(s_vfxCurveLut, 9)") != std::string::npos);
    CHECK(initSource.find("SAMPLER2D(s_vfxGradientLut, 10)") != std::string::npos);
    CHECK(updateSource.find("texture2DLod(s_vfxCurveLut") != std::string::npos);
    CHECK(updateSource.find("texture2DLod(s_vfxGradientLut") != std::string::npos);

    bool curveBinding = false;
    bool gradientBinding = false;
    for (const vfx::detail::KernelBinding& binding : system.kernels[3].shader.bindings) {
        if (binding.kind != vfx::detail::BindingKind::Texture) {
            continue;
        }
        if (binding.stage == 9u && binding.index == 3u) {
            curveBinding = true;
        }
        if (binding.stage == 10u && binding.index == 4u) {
            gradientBinding = true;
        }
    }
    CHECK(curveBinding);
    CHECK(gradientBinding);

    const auto blobs = makeBlobs(compiled);
    vfx::CompileOptions options;
    std::string error;
    const std::vector<std::byte> binary = vfx::detail::writeAssetBinary(compiled, blobs, options, error);
    REQUIRE(error.empty());
    std::string parseError;
    const std::shared_ptr<const vfx::Asset> asset = vfx::parseAssetBinary(binary, parseError);
    INFO(parseError);
    REQUIRE(asset != nullptr);
    REQUIRE(asset->properties.size() == 5u);
    const std::vector<std::byte>& curve = asset->properties[3].lut;
    const std::vector<std::byte>& gradient = asset->properties[4].lut;
    REQUIRE(curve.size() == 256u * 8u);
    REQUIRE(gradient.size() == 256u * 8u);
    CHECK(halfToFloat(readU16(curve, 0u)) == doctest::Approx(0.2f).epsilon(0.001));
    CHECK(halfToFloat(readU16(curve, 255u * 8u)) == doctest::Approx(0.0f));
    CHECK(halfToFloat(readU16(curve, 127u * 8u)) == doctest::Approx(0.9968627f).epsilon(0.001));
    CHECK(halfToFloat(readU16(gradient, 0u)) == doctest::Approx(1.0f));
    CHECK(halfToFloat(readU16(gradient, 2u)) == doctest::Approx(1.0f));
    CHECK(halfToFloat(readU16(gradient, 4u)) == doctest::Approx(1.0f));
    CHECK(halfToFloat(readU16(gradient, 6u)) == doctest::Approx(1.0f));
    CHECK(halfToFloat(readU16(gradient, 255u * 8u)) == doctest::Approx(1.0f));
    CHECK(halfToFloat(readU16(gradient, 255u * 8u + 2u)) == doctest::Approx(0.0f));
    CHECK(halfToFloat(readU16(gradient, 255u * 8u + 4u)) == doctest::Approx(0.0f));
    CHECK(halfToFloat(readU16(gradient, 255u * 8u + 6u)) == doctest::Approx(0.0f));
}

TEST_CASE("vfx compiler samples curve and gradient LUTs from expressions") {
    vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/data/expressions.vfx.json");
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset compiled = vfx::detail::generatePrograms(std::move(parsed.graph));
    REQUIRE(compiled.error.empty());
    const vfx::detail::CompiledSystem& system = compiled.systems.front();
    REQUIRE(system.kernels.size() >= 4u);
    CHECK(system.kernels[2].shader.source.find("texture2DLod(s_vfxGradientLut") != std::string::npos);
    CHECK(system.kernels[3].shader.source.find("texture2DLod(s_vfxCurveLut") != std::string::npos);
    CHECK(system.kernels[2].shader.bindings.size() == 5u);
    CHECK(system.kernels[3].shader.bindings.size() == 4u);
}

TEST_CASE("vfx compiler emits lit output shaders") {
    constexpr std::string_view json = R"json({
      "version": 1, "name": "Lit",
      "properties": [
        {"name": "Metallic", "type": "float", "default": 0.25},
        {"name": "Roughness", "type": "float", "default": 0.35}
      ],
      "systems": [{"name": "Main", "capacity": 64,
        "init": {"blocks": [{"type": "set_size", "size": 0.5}]},
        "outputs": [{"type": "quad", "lit": true, "blend": "alpha",
                     "metallic": "Metallic", "roughness": 0.6}]}]
    })json";
    vfx::ParseResult parsed = vfx::parseGraphJson(json, "lit.vfx.json");
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    INFO(asset.error);
    REQUIRE(asset.error.empty());
    const vfx::detail::CompiledSystem& system = asset.systems.front();
    REQUIRE(system.outputs.size() == 1u);
    const vfx::detail::OutputProgram& output = system.outputs.front();
    CHECK(output.lit);
    CHECK(output.metallicProperty == "Metallic");
    CHECK(output.roughnessProperty.empty());
    CHECK(output.roughness == doctest::Approx(0.6));
    CHECK(output.fragment.source.find("#define HBG_CONTACT_SHADOW_TEXTURE 1") != std::string::npos);
    CHECK(output.fragment.source.find("hbgEvaluateLightLoop") != std::string::npos);
    CHECK(output.fragment.source.find("hbgSampleDirectionalShadow") != std::string::npos);
    CHECK(output.fragment.source.find("uniform vec4 u_vfxProp_Metallic") != std::string::npos);
    CHECK(output.fragment.source.find("surfaceData.metallic = hbgSaturate((u_vfxProp_Metallic.x))") !=
          std::string::npos);
    CHECK(output.fragment.source.find("surfaceData.perceptualRoughness = hbgSaturate(0.6") !=
          std::string::npos);
    CHECK(output.fragment.source.find("HBG_SHADOW_POINT_SAMPLER") == std::string::npos);
    CHECK(output.fragment.source.find("HBG_SHADOW_AREA_SAMPLER") == std::string::npos);
    REQUIRE(output.fragment.bindings.size() == 1u);
    CHECK(output.fragment.bindings[0].stage == 0u);
    REQUIRE(output.vertex.bindings.size() == 2u);
    CHECK(output.vertex.bindings[0].stage == 12u);
    CHECK(output.vertex.bindings[1].stage == 13u);

    const std::string shaderc = environmentValue("VFX_TEST_SHADERC");
    const std::string bgfxSource = environmentValue("VFX_TEST_BGFX_SOURCE");
    const std::string shaderPack = environmentValue("VFX_TEST_SHADER_PACK");
    if (!shaderc.empty() && !bgfxSource.empty() && !shaderPack.empty()) {
        vfx::CompileOptions options;
        options.shaderc = shaderc;
        options.bgfxSource = bgfxSource;
        options.shaderPack = shaderPack;
        const vfx::CompileResult result = vfx::compileGraph(json, options, "lit.vfx.json");
        INFO(result.error);
        REQUIRE(result.ok());
        CHECK(result.binary.size() > kHeaderSize);
        CHECK(readU32(result.binary, 12) == 9u);
    }
}

TEST_CASE("vfx compiler emits soft particle output shaders") {
    constexpr std::string_view json = R"json({
      "version": 1, "name": "Soft",
      "systems": [{"name": "Main", "capacity": 64,
        "init": {"blocks": [{"type": "set_size", "size": 0.5}]},
        "outputs": [{"type": "quad", "lit": true, "blend": "alpha",
                     "softParticles": true, "softDistance": 0.75}]}]
    })json";
    vfx::ParseResult parsed = vfx::parseGraphJson(json, "soft.vfx.json");
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    INFO(asset.error);
    REQUIRE(asset.error.empty());
    const vfx::detail::OutputProgram& output = asset.systems.front().outputs.front();
    CHECK(output.softParticles);
    CHECK(output.softDistance == doctest::Approx(0.75f));
    REQUIRE(output.fragment.bindings.size() == 2u);
    CHECK(output.fragment.bindings[1].kind == vfx::detail::BindingKind::Texture);
    CHECK(output.fragment.bindings[1].stage == 4u);
    CHECK(output.fragment.bindings[1].index == vfx::kSceneDepthBindingIndex);
    CHECK(output.fragment.source.find("SAMPLER2D(s_vfxSceneDepth, 4)") != std::string::npos);
    CHECK(output.fragment.source.find("u_vfxSoftParams") != std::string::npos);
    CHECK(output.fragment.source.find("u_global_invViewProj") != std::string::npos);
    CHECK(output.fragment.source.find("surfaceData.alpha *= softFade") != std::string::npos);

    constexpr std::string_view unlitJson = R"json({
      "version": 1, "name": "SoftUnlit",
      "systems": [{"name": "Main", "capacity": 64,
        "outputs": [{"type": "quad", "blend": "additive",
                     "softParticles": true, "softDistance": 0.4}]}]
    })json";
    vfx::ParseResult unlitParsed = vfx::parseGraphJson(unlitJson, "soft_unlit.vfx.json");
    REQUIRE(unlitParsed.ok());
    vfx::detail::CompiledAsset unlitAsset =
        vfx::detail::generatePrograms(std::move(unlitParsed.graph));
    INFO(unlitAsset.error);
    REQUIRE(unlitAsset.error.empty());
    const vfx::detail::OutputProgram& unlitOutput = unlitAsset.systems.front().outputs.front();
    CHECK(unlitOutput.fragment.source.find("#include \"common/common.sc\"") != std::string::npos);
    CHECK(unlitOutput.fragment.source.find("color *= softFade") != std::string::npos);

    const std::string shaderc = environmentValue("VFX_TEST_SHADERC");
    const std::string bgfxSource = environmentValue("VFX_TEST_BGFX_SOURCE");
    const std::string shaderPack = environmentValue("VFX_TEST_SHADER_PACK");
    if (!shaderc.empty() && !bgfxSource.empty() && !shaderPack.empty()) {
        vfx::CompileOptions options;
        options.shaderc = shaderc;
        options.bgfxSource = bgfxSource;
        options.shaderPack = shaderPack;
        const vfx::CompileResult result = vfx::compileGraph(json, options, "soft.vfx.json");
        INFO(result.error);
        REQUIRE(result.ok());
        CHECK(result.binary.size() > kHeaderSize);
    }
}

TEST_CASE("vfx compiler reports unsupported V1 features with paths") {
    const std::string sorted = R"json({
      "version": 1, "name": "Sorted", "properties": [],
      "systems": [{"name": "Main", "capacity": 64,
        "sort": {"mode": "distance", "order": "ascending"},
        "outputs": [{"type": "quad", "sort": "distance"}]}]
    })json";
    vfx::ParseResult sortedParsed = vfx::parseGraphJson(sorted);
    REQUIRE(sortedParsed.ok());
    vfx::detail::CompiledAsset sortedAsset =
        vfx::detail::generatePrograms(std::move(sortedParsed.graph));
    REQUIRE(sortedAsset.error.empty());
    REQUIRE(sortedAsset.systems.size() == 1u);
    const vfx::detail::CompiledSystem& sortedSystem = sortedAsset.systems.front();
    REQUIRE(sortedSystem.kernels.size() == 9u);
    CHECK(sortedSystem.kernels[5].task == vfx::detail::KernelTask::SortBitonic);
    CHECK(sortedSystem.kernels[6].task == vfx::detail::KernelTask::FillIndirect);
    CHECK(sortedSystem.kernels[5].shader.source.find("sortLess") != std::string::npos);
    CHECK(sortedSystem.kernels[5].shader.source.find("SHARED float s_sortKeys") !=
          std::string::npos);
    CHECK(sortedSystem.kernels[4].shader.source.find("u_global_cameraPos") != std::string::npos);
    CHECK(sortedSystem.kernels[4].shader.source.find("rawStoreFloat(s_vfxRenderKVP") !=
          std::string::npos);
    CHECK(static_cast<int>(sortedSystem.outputs.front().sortMode) ==
          static_cast<int>(vfx::SortMode::Distance));

    const std::string merged = R"json({
      "version": 1, "name": "Merged", "properties": [],
      "systems": [{"name": "Main", "capacity": 8192,
        "sort": {"mode": "distance", "order": "descending"},
        "outputs": [{"type": "quad", "sort": "distance"}]}]
    })json";
    vfx::ParseResult mergedParsed = vfx::parseGraphJson(merged);
    REQUIRE(mergedParsed.ok());
    vfx::detail::CompiledAsset mergedAsset =
        vfx::detail::generatePrograms(std::move(mergedParsed.graph));
    REQUIRE(mergedAsset.error.empty());
    const vfx::detail::CompiledSystem& mergedSystem = mergedAsset.systems.front();
    REQUIRE(mergedSystem.kernels.size() == 10u);
    CHECK(mergedSystem.kernels[5].task == vfx::detail::KernelTask::SortPrepass);
    CHECK(mergedSystem.kernels[5].groups[0] == 1u);
    CHECK(mergedSystem.kernels[6].task == vfx::detail::KernelTask::SortMerge);
    CHECK(mergedSystem.kernels[6].groups[0] == 128u);
    CHECK(mergedSystem.kernels[6].shader.source.find("loadSource") != std::string::npos);
    CHECK(mergedSystem.kernels[6].shader.source.find("sortLess") != std::string::npos);
    CHECK(mergedSystem.kernels[5].shader.source.find("u_vfx_ids.z") != std::string::npos);

    const std::string custom = R"json({
      "version": 1, "name": "CustomSort", "properties": [],
      "attributes": [{"name": "sortKey", "type": "float"}],
      "systems": [{"name": "Main", "capacity": 64,
        "sort": {"mode": "custom", "order": "descending"},
        "outputs": [{"type": "quad", "sort": "custom"}]}]
    })json";
    vfx::ParseResult customParsed = vfx::parseGraphJson(custom);
    REQUIRE(customParsed.ok());
    vfx::detail::CompiledAsset customAsset =
        vfx::detail::generatePrograms(std::move(customParsed.graph));
    REQUIRE(customAsset.error.empty());
    CHECK(customAsset.systems.front().kernels[4].shader.source.find("attributes.Load_sortKey()") !=
          std::string::npos);

    const std::string missingCustom = R"json({
      "version": 1, "name": "MissingCustomSort", "properties": [],
      "systems": [{"name": "Main", "capacity": 64,
        "sort": {"mode": "custom", "order": "ascending"},
        "outputs": [{"type": "quad", "sort": "custom"}]}]
    })json";
    vfx::ParseResult missingParsed = vfx::parseGraphJson(missingCustom);
    REQUIRE(missingParsed.ok());
    vfx::detail::CompiledAsset missingAsset =
        vfx::detail::generatePrograms(std::move(missingParsed.graph));
    CHECK(missingAsset.error.find("systems[0].sort.mode") != std::string::npos);
}

TEST_CASE("vfx compileGraph requires an explicit shader toolchain") {
    const vfx::CompileResult result = vfx::compileGraph("{}", vfx::CompileOptions{});
    CHECK_FALSE(result.ok());
    CHECK(result.error.find("shaderc") != std::string::npos);
}

TEST_CASE("vfx compileGraph bakes and checks a minimal asset when tooling is available") {
    const std::string shaderc = environmentValue("VFX_TEST_SHADERC");
    const std::string bgfxSource = environmentValue("VFX_TEST_BGFX_SOURCE");
    if (shaderc.empty() || bgfxSource.empty()) {
        return;
    }
    vfx::CompileOptions options;
    options.shaderc = shaderc;
    options.bgfxSource = bgfxSource;
    const vfx::CompileResult result =
        vfx::compileGraphFile(sourcePath() / "tests/assets/minimal_unlit.vfx.json", options);
    REQUIRE(result.ok());
    REQUIRE(result.binary.size() > kHeaderSize);
    CHECK(result.binary[0] == std::byte{'V'});
    CHECK(result.binary[1] == std::byte{'F'});
    CHECK(result.binary[2] == std::byte{'X'});
    CHECK(result.binary[3] == std::byte{'B'});
    CHECK(readU16(result.binary, 4) == 6u);
    CHECK(readU16(result.binary, 6) == kHeaderSize);
    CHECK(readU32(result.binary, 12) == 9u);

    std::size_t kernelsOffset = 0;
    std::size_t kernelsSize = 0;
    for (std::uint32_t i = 0; i < readU32(result.binary, 12); ++i) {
        const std::size_t table = kHeaderSize + i * 16u;
        if (readU32(result.binary, table) == 6u) {
            kernelsOffset = readU32(result.binary, table + 4u);
            kernelsSize = readU32(result.binary, table + 8u);
            break;
        }
    }
    REQUIRE(kernelsOffset != 0u);
    const std::uint32_t kernelCount = readU32(result.binary, kernelsOffset);
    CHECK(kernelCount == 16u);
    std::uint32_t dxbcCount = 0;
    std::uint32_t spirvCount = 0;
    for (std::uint32_t i = 0; i < kernelCount; ++i) {
        const std::size_t record = kernelsOffset + 4u + i * 44u;
        const auto backend = static_cast<vfx::ShaderBackend>(static_cast<std::uint8_t>(result.binary[record + 6u]));
        const std::uint32_t blobOffset = readU32(result.binary, record + 28u);
        const std::uint32_t blobSize = readU32(result.binary, record + 32u);
        CHECK(blobOffset + blobSize <= kernelsSize);
        REQUIRE(blobSize > 4u);
        const std::byte* blob = result.binary.data() + kernelsOffset + blobOffset;
        const auto stage = static_cast<vfx::ShaderStage>(static_cast<std::uint8_t>(result.binary[record + 5u]));
        const char expectedPrefix = stage == vfx::ShaderStage::Compute
                                        ? 'C'
                                        : stage == vfx::ShaderStage::Vertex ? 'V' : 'F';
        CHECK(static_cast<std::uint8_t>(blob[0]) == static_cast<std::uint8_t>(expectedPrefix));
        CHECK(static_cast<std::uint8_t>(blob[1]) == static_cast<std::uint8_t>('S'));
        CHECK(static_cast<std::uint8_t>(blob[2]) == static_cast<std::uint8_t>('H'));
        if (backend == vfx::ShaderBackend::Dxbc) {
            ++dxbcCount;
        } else {
            ++spirvCount;
        }
    }
    CHECK(dxbcCount == 8u);
    CHECK(spirvCount == 8u);
    const std::string binaryText(reinterpret_cast<const char*>(result.binary.data()), result.binary.size());
    CHECK(binaryText.find("Unity") == std::string::npos);
    CHECK(binaryText.find("C:/") == std::string::npos);
}

TEST_CASE("vfx compileGraph bakes force and turbulence shaders when tooling is available") {
    const std::string shaderc = environmentValue("VFX_TEST_SHADERC");
    const std::string bgfxSource = environmentValue("VFX_TEST_BGFX_SOURCE");
    if (shaderc.empty() || bgfxSource.empty()) {
        return;
    }
    vfx::CompileOptions options;
    options.shaderc = shaderc;
    options.bgfxSource = bgfxSource;
    const vfx::CompileResult result =
        vfx::compileGraphFile(sourcePath() / "tests/assets/forces_1024.vfx.json", options);
    REQUIRE(result.ok());
    CHECK(result.binary.size() > kHeaderSize);
    CHECK(readU32(result.binary, 12) == 9u);
}


TEST_CASE("vfx compiler expands subgraphs to equivalent kernels") {
    constexpr std::string_view withSubgraphs = R"json(
    {
      "version": 1,
      "name": "SubgraphEquivalence",
      "subgraphs": [
        {
          "name": "BlendColor",
          "inputs": [
            {"name": "Color", "type": "vector3", "default": [0.25, 0.5, 1.0]},
            {"name": "Offset", "type": "vector3", "default": [0.125, 0.25, 0.0]}
          ],
          "outputs": [
            {"name": "Result", "type": "vector3",
             "value": {"op": "add", "a": {"op": "input", "name": "Color"},
                       "b": {"op": "input", "name": "Offset"}}}
          ]
        },
        {
          "name": "Fall",
          "inputs": [
            {"name": "Gravity", "type": "vector3", "default": [0, -9.81, 0]},
            {"name": "Drag", "type": "float", "default": 0.25}
          ],
          "outputs": [],
          "update": {"blocks": [
            {"type": "gravity", "gravity": {"op": "input", "name": "Gravity"}},
            {"type": "drag", "drag": {"op": "input", "name": "Drag"}},
            {"type": "integrate"}
          ]}
        }
      ],
      "systems": [{
        "name": "Main",
        "capacity": 64,
        "init": {"blocks": [
          {"type": "set_color", "color": {"op": "subgraph", "name": "BlendColor",
            "args": {"Color": [0.25, 0.5, 1.0]}}}
        ]},
        "update": {"blocks": [
          {"type": "subgraph", "name": "Fall", "args": {"Gravity": [0, -4, 0]}},
          {"type": "age"}
        ]},
        "outputs": [{"type": "quad", "lit": false, "blend": "alpha"}]
      }]
    }
    )json";
    constexpr std::string_view flattened = R"json(
    {
      "version": 1,
      "name": "SubgraphEquivalence",
      "systems": [{
        "name": "Main",
        "capacity": 64,
        "init": {"blocks": [
          {"type": "set_color", "color": [0.375, 0.75, 1.0]}
        ]},
        "update": {"blocks": [
          {"type": "gravity", "gravity": [0, -4, 0]},
          {"type": "drag", "drag": 0.25},
          {"type": "integrate"},
          {"type": "age"}
        ]},
        "outputs": [{"type": "quad", "lit": false, "blend": "alpha"}]
      }]
    }
    )json";

    vfx::ParseResult nested = vfx::parseGraphJson(withSubgraphs);
    vfx::ParseResult direct = vfx::parseGraphJson(flattened);
    REQUIRE(nested.ok());
    REQUIRE(direct.ok());
    vfx::detail::CompiledAsset nestedAsset = vfx::detail::generatePrograms(std::move(nested.graph));
    vfx::detail::CompiledAsset directAsset = vfx::detail::generatePrograms(std::move(direct.graph));
    INFO(nestedAsset.error);
    INFO(directAsset.error);
    REQUIRE(nestedAsset.error.empty());
    REQUIRE(directAsset.error.empty());
    REQUIRE(nestedAsset.systems.size() == 1u);
    REQUIRE(directAsset.systems.size() == 1u);
    const vfx::detail::CompiledSystem& nestedSystem = nestedAsset.systems[0];
    const vfx::detail::CompiledSystem& directSystem = directAsset.systems[0];
    REQUIRE(nestedSystem.kernels.size() == directSystem.kernels.size());
    for (std::size_t i = 0; i < nestedSystem.kernels.size(); ++i) {
        CHECK(nestedSystem.kernels[i].shader.source == directSystem.kernels[i].shader.source);
        CHECK(nestedSystem.kernels[i].shader.varyings == directSystem.kernels[i].shader.varyings);
    }
    REQUIRE(nestedSystem.outputs.size() == directSystem.outputs.size());
    CHECK(nestedSystem.outputs[0].vertex.source == directSystem.outputs[0].vertex.source);
    CHECK(nestedSystem.outputs[0].fragment.source == directSystem.outputs[0].fragment.source);
}

TEST_CASE("vfx compiler emits custom HLSL inside a scoped kernel block") {
    constexpr std::string_view json = R"json(
    {"version":1,"name":"CustomHLSL",
     "properties":[{"name":"Scale","type":"float","default":2.0}],
     "systems":[{"name":"Main","capacity":8,
       "init":{"blocks":[
         {"type":"custom_hlsl","code":"VFX_SET_ATTR(position, float3(0.0, 1.0, 0.0));",
          "writes":["position"]}]},
       "update":{"blocks":[
         {"type":"custom_hlsl",
          "code":"VFX_SET_ATTR(position, VFX_ATTR(position) + float3(0.0, VFX_DT, 0.0) * VFX_PROP(Scale));",
          "reads":["position"],"writes":["position"]},
         {"type":"age"}]},
       "outputs":[{}]}]}
    )json";
    vfx::ParseResult parsed = vfx::parseGraphJson(json);
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    INFO(asset.error);
    REQUIRE(asset.error.empty());
    REQUIRE(asset.systems.size() == 1u);
    const vfx::detail::CompiledSystem& system = asset.systems[0];
    CHECK(system.usage.writes.count("position") == 1u);
    CHECK(system.usage.reads.count("position") == 1u);

    const vfx::detail::KernelProgram* init = nullptr;
    const vfx::detail::KernelProgram* update = nullptr;
    for (const vfx::detail::KernelProgram& kernel : system.kernels) {
        if (kernel.task == vfx::detail::KernelTask::Init) {
            init = &kernel;
        } else if (kernel.task == vfx::detail::KernelTask::Update) {
            update = &kernel;
        }
    }
    REQUIRE(init != nullptr);
    REQUIRE(update != nullptr);
    CHECK(init->shader.source.find("VFX_SET_ATTR(position, float3(0.0, 1.0, 0.0));") !=
          std::string::npos);
    CHECK(update->shader.source.find("VFX_ATTR(position) + float3(0.0, VFX_DT, 0.0) * VFX_PROP(Scale)") !=
          std::string::npos);
    CHECK(update->shader.source.find("#define VFX_SET_ATTR(name, value) a_##name = (value)") !=
          std::string::npos);
    CHECK(update->shader.source.find("#define VFX_PROP_Scale") != std::string::npos);
    CHECK(update->shader.source.find("custom_hlsl") == std::string::npos);
}

TEST_CASE("vfx compileGraphFile bakes subgraph and custom HLSL fixtures for both backends when tooling is available") {
    const std::string shaderc = environmentValue("VFX_TEST_SHADERC");
    const std::string bgfxSource = environmentValue("VFX_TEST_BGFX_SOURCE");
    if (shaderc.empty() || bgfxSource.empty()) {
        return;
    }
    vfx::CompileOptions options;
    options.shaderc = shaderc;
    options.bgfxSource = bgfxSource;
    for (const char* name : {"subgraph_visual", "custom_hlsl_visual"}) {
        INFO(name);
        const vfx::CompileResult result =
            vfx::compileGraphFile(sourcePath() / "tests/assets" / (std::string(name) + ".vfx.json"), options);
        REQUIRE(result.ok());
        REQUIRE(result.binary.size() > kHeaderSize);
        CHECK(result.binary[0] == std::byte{'V'});
        CHECK(result.binary[1] == std::byte{'F'});
        CHECK(result.binary[2] == std::byte{'X'});
        CHECK(result.binary[3] == std::byte{'B'});
        CHECK(readU16(result.binary, 4) == 6u);
        CHECK(readU32(result.binary, 12) == 9u);
        const std::string binaryText(reinterpret_cast<const char*>(result.binary.data()),
                                     result.binary.size());
        CHECK(binaryText.find("C:/") == std::string::npos);
    }
}

TEST_CASE("vfx compileGraph bakes custom HLSL for both backends when tooling is available") {
    const std::string shaderc = environmentValue("VFX_TEST_SHADERC");
    const std::string bgfxSource = environmentValue("VFX_TEST_BGFX_SOURCE");
    if (shaderc.empty() || bgfxSource.empty()) {
        return;
    }
    constexpr std::string_view json = R"json(
    {"version":1,"name":"CustomHLSLBake",
     "systems":[{"name":"Main","capacity":8,
       "init":{"blocks":[{"type":"custom_hlsl",
         "code":"VFX_SET_ATTR(position, float3(0.0, 1.0, 0.0));","writes":["position"]}]},
       "update":{"blocks":[{"type":"custom_hlsl",
         "code":"VFX_SET_ATTR(position, VFX_ATTR(position) + float3(0.0, VFX_DT, 0.0));",
         "reads":["position"],"writes":["position"]},{"type":"age"}]},
       "outputs":[{}]}]}
    )json";
    vfx::CompileOptions options;
    options.shaderc = shaderc;
    options.bgfxSource = bgfxSource;
    const vfx::CompileResult result = vfx::compileGraph(json, options);
    REQUIRE(result.ok());
    REQUIRE(result.binary.size() > kHeaderSize);
    CHECK(readU32(result.binary, 12) == 9u);
}
