#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "vfx/runtime.h"
#include "support/mock_backend.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::filesystem::path sourcePath() {
#ifdef VFX_SOURCE_DIR
    return std::filesystem::path(VFX_SOURCE_DIR);
#else
    return std::filesystem::path(".");
#endif
}

std::shared_ptr<const vfx::Asset> loadMinimal(vfx::Runtime& runtime, vfx::test::MockBackend& backend) {
    std::string error;
    std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/minimal_unlit.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    REQUIRE(backend.liveBufferCount() == 0u);
    return asset;
}

vfx::UniformHandle findUniform(const vfx::test::MockBackend& backend, std::string_view name) {
    for (vfx::Index i = 0; i < backend.uniforms().size(); ++i) {
        if (backend.uniforms()[i].name == name) {
            return vfx::UniformHandle{i};
        }
    }
    return {};
}

const vfx::test::MockCommand* findUniformCommand(const vfx::test::MockBackend& backend,
                                                 vfx::UniformHandle uniform) {
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::SetUniform && command.uniform == uniform) {
            return &command;
        }
    }
    return nullptr;
}

std::uint32_t countCommands(const vfx::test::MockBackend& backend,
                            vfx::test::MockCommand::Type type) {
    std::uint32_t count = 0;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == type) {
            ++count;
        }
    }
    return count;
}

vfx::BufferHandle bufferByName(const vfx::test::MockBackend& backend, std::string_view name) {
    for (vfx::Index i = 0; i < backend.buffers().size(); ++i) {
        if (backend.buffers()[i].alive && backend.buffers()[i].name == name) {
            return vfx::BufferHandle{i};
        }
    }
    return {};
}

} // namespace

TEST_CASE("vfx runtime loads a baked asset and owns effect resources") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);

    vfx::EffectDesc desc;
    desc.transform = vfx::Mat4::identity();
    std::string error;
    vfx::Effect* effect = runtime.createEffect(*asset, desc, &error);
    INFO(error);
    REQUIRE(effect != nullptr);

    CHECK(backend.liveBufferCount() == 7u);
    CHECK(backend.liveProgramCount() == 7u);
    CHECK(backend.liveTextureCount() == 1u);
    CHECK(runtime.diagnostics().effects == 1u);
    CHECK(effect->isAlive());

    const vfx::Bounds bounds = effect->bounds();
    CHECK(bounds.valid);
    CHECK(bounds.min.x == doctest::Approx(-4.0f));
    CHECK(bounds.min.y == doctest::Approx(-3.0f));
    CHECK(bounds.max.x == doctest::Approx(4.0f));
    CHECK(bounds.max.y == doctest::Approx(5.0f));

    runtime.destroyEffect(effect);
    CHECK(backend.liveBufferCount() == 0u);
    CHECK(backend.liveProgramCount() == 0u);
    CHECK(backend.liveTextureCount() == 0u);
    CHECK(backend.destroyedBufferCount() == 7u);
    CHECK(backend.destroyedProgramCount() == 7u);
    CHECK(backend.destroyedTextureCount() == 1u);
}

TEST_CASE("vfx runtime schedules spawning and emits the simulation command sequence") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);
    vfx::Effect* effect = runtime.createEffect(*asset, {});
    REQUIRE(effect != nullptr);

    runtime.update(0.1f);
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::BeginPass) == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 6u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::EndPass) == 1u);
    CHECK(runtime.diagnostics().dispatches == 6u);
    CHECK(runtime.diagnostics().particles == 12u);

    const vfx::UniformHandle params = findUniform(backend, "u_vfx_params");
    REQUIRE(params.valid());
    const vfx::test::MockCommand* paramsCommand = findUniformCommand(backend, params);
    REQUIRE(paramsCommand != nullptr);
    REQUIRE(paramsCommand->data.size() == sizeof(vfx::Vec4));
    const auto* values = reinterpret_cast<const float*>(paramsCommand->data.data());
    CHECK(values[0] == doctest::Approx(0.1f));
    CHECK(values[1] == doctest::Approx(0.1f));
    CHECK(values[2] == doctest::Approx(12.0f));
    CHECK(values[3] == doctest::Approx(0.0f));

    backend.clearCommands();
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 5u);
    const vfx::test::MockCommand* secondParams = findUniformCommand(backend, params);
    REQUIRE(secondParams != nullptr);
    const auto* secondValues = reinterpret_cast<const float*>(secondParams->data.data());
    CHECK(secondValues[2] == doctest::Approx(4.0f));

    backend.clearCommands();
    runtime.encodeOutput(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::BeginPass) == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::SetStorageBuffer) == 2u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::SetTexture) == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::SetState) == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::EndPass) == 1u);
    CHECK(runtime.diagnostics().draws == 1u);
}

TEST_CASE("vfx runtime merges spawner bursts and clamps large frame deltas") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/spawner_bursts.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);

    const vfx::UniformHandle params = findUniform(backend, "u_vfx_params");
    REQUIRE(params.valid());
    const auto encodeSpawnCount = [&](float deltaSeconds) {
        runtime.update(deltaSeconds);
        backend.clearCommands();
        runtime.encodeSimulation(backend, {});
        const vfx::test::MockCommand* command = findUniformCommand(backend, params);
        REQUIRE(command != nullptr);
        REQUIRE(command->data.size() == sizeof(vfx::Vec4));
        return reinterpret_cast<const float*>(command->data.data())[2];
    };

    CHECK(encodeSpawnCount(0.1f) == doctest::Approx(14.0f));
    const std::vector<float> expected = {2.0f, 2.0f, 2.0f, 2.0f, 0.0f, 0.0f, 0.0f};
    for (const float spawnCount : expected) {
        CHECK(encodeSpawnCount(0.1f) == doctest::Approx(spawnCount));
    }

    effect->stop();
    effect->play();
    CHECK(encodeSpawnCount(0.1f) == doctest::Approx(14.0f));
    effect->stop();
    effect->play();
    CHECK(encodeSpawnCount(1.0f) == doctest::Approx(14.0f));
    runtime.destroyEffect(effect);
}

TEST_CASE("vfx runtime emits deterministic dispatch order and buffer bindings") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);
    vfx::Effect* effect = runtime.createEffect(*asset, {});
    REQUIRE(effect != nullptr);
    const std::array<const char*, 7> names = {
        "vfx.Sparks.buffer0", "vfx.Sparks.buffer1", "vfx.Sparks.buffer2", "vfx.Sparks.buffer3",
        "vfx.Sparks.buffer4", "vfx.Sparks.buffer5", "vfx.Sparks.buffer6",
    };
    std::array<vfx::BufferHandle, 7> buffers{};
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        buffers[i] = bufferByName(backend, names[i]);
        REQUIRE(buffers[i].valid());
    }
    using Binding = std::pair<std::uint8_t, vfx::BufferHandle>;
    const std::vector<std::vector<Binding>> resetSequence = {
        {{0, buffers[4]}, {1, buffers[3]}},
        {{0, buffers[1]}, {1, buffers[0]}, {2, buffers[2]}},
        {{0, buffers[0]}, {1, buffers[1]}, {2, buffers[4]}},
        {{0, buffers[0]}, {1, buffers[1]}},
        {{0, buffers[0]}, {1, buffers[2]}, {2, buffers[3]}},
        {{0, buffers[5]}, {1, buffers[3]}},
    };
    const std::vector<std::vector<Binding>> continuousSequence = {
        {{0, buffers[4]}, {1, buffers[3]}},
        {{0, buffers[0]}, {1, buffers[1]}, {2, buffers[4]}},
        {{0, buffers[0]}, {1, buffers[1]}},
        {{0, buffers[0]}, {1, buffers[2]}, {2, buffers[3]}},
        {{0, buffers[5]}, {1, buffers[3]}},
    };
    const auto verifySimulation = [&](std::uint32_t expectedDispatches, bool reset) {
        const std::vector<std::vector<Binding>>& sequence = reset ? resetSequence : continuousSequence;
        std::size_t kernel = 0;
        std::vector<Binding> bindings;
        for (const vfx::test::MockCommand& command : backend.commands()) {
            if (command.type == vfx::test::MockCommand::Type::SetStorageBuffer) {
                bindings.emplace_back(command.stage, command.buffer);
            } else if (command.type == vfx::test::MockCommand::Type::Dispatch) {
                REQUIRE(kernel < sequence.size());
                CHECK(bindings == sequence[kernel]);
                ++kernel;
                bindings.clear();
            } else if (command.type == vfx::test::MockCommand::Type::EndPass) {
                CHECK(bindings.empty());
            }
        }
        CHECK(kernel == expectedDispatches);
        CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == expectedDispatches);
        CHECK(countCommands(backend, vfx::test::MockCommand::Type::SetStorageBuffer) ==
              (reset ? 15u : 12u));
        CHECK(countCommands(backend, vfx::test::MockCommand::Type::BeginPass) == 1u);
        CHECK(countCommands(backend, vfx::test::MockCommand::Type::EndPass) == 1u);
        CHECK(countCommands(backend, vfx::test::MockCommand::Type::SetUniform) == 4u);
    };

    runtime.update(0.1f);
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    verifySimulation(6u, true);

    backend.clearCommands();
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    verifySimulation(5u, false);

    backend.clearCommands();
    runtime.encodeOutput(backend, {});
    REQUIRE(backend.commands().size() == 11u);
    CHECK(backend.commands()[0].type == vfx::test::MockCommand::Type::BeginPass);
    CHECK(backend.commands()[1].type == vfx::test::MockCommand::Type::SetUniform);
    CHECK(backend.commands()[2].type == vfx::test::MockCommand::Type::SetUniform);
    CHECK(backend.commands()[3].type == vfx::test::MockCommand::Type::SetUniform);
    CHECK(backend.commands()[4].type == vfx::test::MockCommand::Type::SetUniform);
    CHECK(backend.commands()[5].type == vfx::test::MockCommand::Type::SetStorageBuffer);
    CHECK(backend.commands()[5].stage == 12u);
    CHECK(backend.commands()[5].buffer == buffers[0]);
    CHECK(backend.commands()[6].type == vfx::test::MockCommand::Type::SetStorageBuffer);
    CHECK(backend.commands()[6].stage == 13u);
    CHECK(backend.commands()[6].buffer == buffers[2]);
    CHECK(backend.commands()[7].type == vfx::test::MockCommand::Type::SetTexture);
    CHECK(backend.commands()[8].type == vfx::test::MockCommand::Type::SetState);
    CHECK(backend.commands()[9].type == vfx::test::MockCommand::Type::DrawIndirect);
    CHECK(backend.commands()[9].buffer == buffers[5]);
    CHECK(backend.commands()[10].type == vfx::test::MockCommand::Type::EndPass);
}

TEST_CASE("vfx runtime applies properties, pause, stop and restart") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);
    vfx::Effect* effect = runtime.createEffect(*asset, {});
    REQUIRE(effect != nullptr);

    CHECK(effect->setFloat("Size", 0.75f));
    CHECK(effect->setColor("Tint", vfx::Vec4{0.1f, 0.2f, 0.3f, 0.4f}));
    CHECK_FALSE(effect->setFloat("Missing", 1.0f));
    CHECK_FALSE(effect->setFloat("Tint", 1.0f));

    runtime.update(0.0f);
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    const vfx::UniformHandle sizeUniform = findUniform(backend, "u_vfxProp_Size");
    const vfx::UniformHandle tintUniform = findUniform(backend, "u_vfxProp_Tint");
    REQUIRE(sizeUniform.valid());
    REQUIRE(tintUniform.valid());
    const vfx::test::MockCommand* sizeCommand = findUniformCommand(backend, sizeUniform);
    const vfx::test::MockCommand* tintCommand = findUniformCommand(backend, tintUniform);
    REQUIRE(sizeCommand != nullptr);
    REQUIRE(tintCommand != nullptr);
    const auto* sizeValues = reinterpret_cast<const float*>(sizeCommand->data.data());
    const auto* tintValues = reinterpret_cast<const float*>(tintCommand->data.data());
    CHECK(sizeValues[0] == doctest::Approx(0.75f));
    CHECK(tintValues[0] == doctest::Approx(0.1f));
    CHECK(tintValues[3] == doctest::Approx(0.4f));

    effect->pause();
    backend.clearCommands();
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    CHECK(backend.commands().empty());

    effect->stop();
    CHECK_FALSE(effect->isAlive());
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 6u);
    backend.clearCommands();
    runtime.encodeOutput(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 0u);

    effect->play();
    CHECK(effect->isAlive());
    runtime.update(0.0f);
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 6u);
}

TEST_CASE("vfx runtime updates curve and gradient LUTs and local bounds") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/curve_unlit.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);

    vfx::EffectDesc desc;
    desc.transform.m[12] = 10.0f;
    vfx::Effect* effect = runtime.createEffect(*asset, desc, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    CHECK(backend.liveTextureCount() == 3u);

    const vfx::Bounds bounds = effect->bounds();
    CHECK(bounds.valid);
    CHECK(bounds.min.x == doctest::Approx(6.0f));
    CHECK(bounds.max.x == doctest::Approx(14.0f));
    CHECK(bounds.min.y == doctest::Approx(-3.0f));
    CHECK(bounds.max.y == doctest::Approx(5.0f));

    std::size_t updates = 0;
    for (const vfx::test::MockBackend::TextureSlot& texture : backend.textures()) {
        updates += texture.updateCount;
    }
    const vfx::CurveKey curve[] = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    const vfx::GradientKey gradient[] = {{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
                                         {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}};
    CHECK(effect->setCurve("SizeCurve", curve));
    CHECK(effect->setGradient("ColorRamp", gradient));
    std::size_t updated = 0;
    for (const vfx::test::MockBackend::TextureSlot& texture : backend.textures()) {
        updated += texture.updateCount;
    }
    CHECK(updated == updates + 2u);

    const vfx::TextureHandle externalTexture = backend.createTexture(
        {1, 1, 1, vfx::TextureUsage::Sampled, vfx::TextureFormat::RGBA8, nullptr, 0, "external"});
    REQUIRE(externalTexture.valid());
    CHECK(effect->setTexture("MainTex", externalTexture));

    runtime.update(0.0f);
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    runtime.encodeOutput(backend, {});
    const vfx::UniformHandle world = findUniform(backend, "u_vfx_world");
    REQUIRE(world.valid());
    const vfx::test::MockCommand* worldCommand = findUniformCommand(backend, world);
    REQUIRE(worldCommand != nullptr);
    REQUIRE(worldCommand->data.size() == sizeof(vfx::Mat4));
    const auto* worldValues = reinterpret_cast<const float*>(worldCommand->data.data());
    CHECK(worldValues[12] == doctest::Approx(10.0f));
    CHECK(worldValues[13] == doctest::Approx(0.0f));
    bool foundTexture = false;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::SetTexture) {
            foundTexture = true;
            CHECK(command.texture == externalTexture);
        }
    }
    CHECK(foundTexture);

    runtime.destroyEffect(effect);
    CHECK(backend.liveTextureCount() == 1u);
    backend.destroyTexture(externalTexture);
    CHECK(backend.liveTextureCount() == 0u);
}

TEST_CASE("vfx runtime reports load errors") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string callbackError;
    runtime.setErrorCallback([&](std::string_view message) { callbackError.assign(message); });

    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/does_not_exist.vfxb", &error);
    CHECK(asset == nullptr);
    CHECK_FALSE(error.empty());
    CHECK_FALSE(callbackError.empty());
}

TEST_CASE("vfx runtime rejects an asset with a broken content hash") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);

    std::ifstream input(sourcePath() / "tests/assets/minimal_unlit.vfxb", std::ios::binary);
    REQUIRE(input.good());
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    REQUIRE(bytes.size() > 64u);
    bytes.back() = static_cast<char>(bytes.back() ^ 0x5a);
    const std::filesystem::path broken =
        std::filesystem::temp_directory_path() / "hbg_vfx_broken_hash.vfxb";
    {
        std::ofstream output(broken, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    std::string error;
    const std::shared_ptr<const vfx::Asset> asset = runtime.loadAsset(broken, &error);
    CHECK(asset == nullptr);
    CHECK(error.find("hash") != std::string::npos);
    std::filesystem::remove(broken);
}

