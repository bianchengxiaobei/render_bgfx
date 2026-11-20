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
    for (const vfx::detail::CompiledSystem& system : asset.systems) {
        for (const vfx::detail::KernelProgram& kernel : system.kernels) {
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
        }
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
    CHECK(system.kernels[0].shader.bindings.size() == 2u);
    CHECK(system.kernels[0].shader.source.find("uint(u_vfx_params.z + 0.5)") != std::string::npos);
    CHECK(system.kernels[2].shader.source.find("rawLoadUint(s_vfxSpawnCount, 0u)") != std::string::npos);
    CHECK(system.kernels[2].shader.bindings.size() == 3u);
    CHECK(system.kernels[4].shader.bindings.size() == 3u);
    CHECK(system.kernels[5].shader.bindings.size() == 2u);
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
    CHECK(readU16(binary, 4) == 1u);
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
    CHECK(readU32(binary, buffersOffset) == 7u);
    CHECK(buffersSize == 4u + 7u * 24u);

    const auto [kernelsOffset, kernelsSize] = section(6);
    CHECK(readU32(binary, kernelsOffset) == 16u);
    CHECK(kernelsSize > 16u * 44u);

    const auto [bindingsOffset, bindingsSize] = section(7);
    CHECK(readU32(binary, bindingsOffset) > 0u);
    CHECK(bindingsSize >= 4u + readU32(binary, bindingsOffset) * 8u);

    const auto [outputsOffset, outputsSize] = section(8);
    CHECK(readU32(binary, outputsOffset) == 1u);
    CHECK(outputsSize == 4u + 24u);

    const auto [eventsOffset, eventsSize] = section(9);
    CHECK(readU32(binary, eventsOffset) == 0u);
    CHECK(eventsSize == 4u);

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
    CHECK(asset->systems[0].buffers.size() == 7u);
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

TEST_CASE("vfx compiler reports unsupported V1 features with paths") {
    const std::string lit = R"json({
      "version": 1, "name": "Lit", "properties": [],
      "systems": [{"name": "Main", "capacity": 64,
        "outputs": [{"type": "quad", "lit": true}]}]
    })json";
    vfx::ParseResult parsed = vfx::parseGraphJson(lit);
    REQUIRE(parsed.ok());
    vfx::detail::CompiledAsset asset = vfx::detail::generatePrograms(std::move(parsed.graph));
    CHECK(asset.error.find("systems[0].outputs[0].lit") != std::string::npos);

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
    CHECK(sortedAsset.error.find("systems[0].sort.mode") != std::string::npos);
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
    CHECK(readU16(result.binary, 4) == 1u);
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

