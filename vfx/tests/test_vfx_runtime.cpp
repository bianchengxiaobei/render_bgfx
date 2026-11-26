#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "vfx/runtime.h"
#include "runtime_asset.h"
#include "support/mock_backend.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
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

const vfx::test::MockCommand* findLastUniformCommand(const vfx::test::MockBackend& backend,
                                                     vfx::UniformHandle uniform) {
    const vfx::test::MockCommand* found = nullptr;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::SetUniform && command.uniform == uniform) {
            found = &command;
        }
    }
    return found;
}

const vfx::test::MockCommand* findFirstCommand(const vfx::test::MockBackend& backend,
                                               vfx::test::MockCommand::Type type) {
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == type) {
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

std::uint32_t readU32(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[offset + i]) << (i * 8u);
    }
    return value;
}

void writeU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::byte>((value >> (i * 8u)) & 0xffu);
    }
}

void writeU64(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        bytes[offset + i] = static_cast<std::byte>((value >> (i * 8u)) & 0xffu);
    }
}

std::vector<std::byte> binaryWithOutputPriorities(const std::filesystem::path& path,
                                                  std::initializer_list<std::int32_t> priorities) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> raw((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    std::memcpy(bytes.data(), raw.data(), raw.size());
    if (bytes.size() < 64u) {
        return {};
    }
    const std::uint32_t sectionCount = readU32(bytes, 12u);
    for (std::uint32_t i = 0; i < sectionCount; ++i) {
        const std::size_t entry = 64u + static_cast<std::size_t>(i) * 16u;
        if (readU32(bytes, entry) != 8u) {
            continue;
        }
        const std::size_t offset = readU32(bytes, entry + 4u);
        const std::uint32_t count = readU32(bytes, offset);
        if (count != priorities.size()) {
            return {};
        }
        std::size_t record = offset + 4u;
        for (const std::int32_t priority : priorities) {
            writeU32(bytes, record + 28u, std::bit_cast<std::uint32_t>(priority));
            record += 32u;
        }
        std::uint64_t hash = 1469598103934665603ull;
        for (std::size_t i = 64u; i < bytes.size(); ++i) {
            hash ^= static_cast<std::uint8_t>(bytes[i]);
            hash *= 1099511628211ull;
        }
        writeU64(bytes, 16u, hash);
        return bytes;
    }
    return {};
}

} // namespace

TEST_CASE("vfx runtime loads a baked asset and owns effect resources") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);
    REQUIRE(asset != nullptr);
    CHECK(asset->systems[0].boundsPadding.x == 0.0f);
    CHECK(asset->systems[0].boundsPadding.y == 0.0f);
    CHECK(asset->systems[0].boundsPadding.z == 0.0f);

    vfx::EffectDesc desc;
    desc.transform = vfx::Mat4::identity();
    std::string error;
    vfx::Effect* effect = runtime.createEffect(*asset, desc, &error);
    INFO(error);
    REQUIRE(effect != nullptr);

    CHECK(backend.liveBufferCount() == 9u);
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
    CHECK(backend.destroyedBufferCount() == 9u);
    CHECK(backend.destroyedProgramCount() == 7u);
    CHECK(backend.destroyedTextureCount() == 1u);
}

TEST_CASE("vfx runtime reports whether loaded assets have lit outputs") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    CHECK_FALSE(runtime.hasLitOutputs());

    const std::shared_ptr<const vfx::Asset> unlit = loadMinimal(runtime, backend);
    CHECK_FALSE(runtime.hasLitOutputs());

    std::string error;
    const std::shared_ptr<const vfx::Asset> lit =
        runtime.loadAsset(sourcePath() / "tests/assets/lit_visual.vfxb", &error);
    INFO(error);
    REQUIRE(lit != nullptr);
    CHECK(runtime.hasLitOutputs());
}

TEST_CASE("vfx runtime reports shadow casting outputs") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    CHECK_FALSE(runtime.hasShadowOutputs());

    std::string error;
    const std::shared_ptr<const vfx::Asset> lit =
        runtime.loadAsset(sourcePath() / "tests/assets/lit_visual.vfxb", &error);
    INFO(error);
    REQUIRE(lit != nullptr);
    CHECK_FALSE(runtime.hasShadowOutputs());
    for (const vfx::AssetProgram& program : lit->programs) {
        if (program.output) {
            CHECK(program.generateMotionVector);
        }
    }

    const std::shared_ptr<const vfx::Asset> shadow =
        runtime.loadAsset(sourcePath() / "tests/assets/shadow_visual.vfxb", &error);
    INFO(error);
    REQUIRE(shadow != nullptr);
    CHECK(runtime.hasShadowOutputs());
    bool foundShadowOutput = false;
    for (const vfx::AssetProgram& program : shadow->programs) {
        if (program.output && program.castShadows) {
            foundShadowOutput = true;
            CHECK_FALSE(program.generateMotionVector);
        }
    }
    CHECK(foundShadowOutput);
}

TEST_CASE("vfx runtime uses the previous effect transform for motion vectors") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/curve_visual.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);

    const std::array<std::byte, 4> dummyShader{};
    const std::array<vfx::ShaderBlob, 2> blobs{{
        {vfx::ShaderStage::Vertex, vfx::ShaderBackend::Dxbc, dummyShader},
        {vfx::ShaderStage::Fragment, vfx::ShaderBackend::Dxbc, dummyShader},
    }};
    const vfx::ProgramHandle motionProgram = backend.createProgram(blobs);
    REQUIRE(motionProgram.valid());
    backend.setOutputProgram(motionProgram);

    vfx::EffectDesc desc;
    desc.transform = vfx::Mat4::identity();
    vfx::Effect* effect = runtime.createEffect(*asset, desc, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    vfx::Mat4 initial = vfx::Mat4::identity();
    initial.m[12] = 1.0f;
    effect->setTransform(initial);
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});

    const auto readMatrix = [](const vfx::test::MockCommand& command, const char* name) {
        INFO(name);
        REQUIRE(command.data.size() == 64u);
        std::array<float, 16> matrix{};
        std::memcpy(matrix.data(), command.data.data(), sizeof(matrix));
        return matrix;
    };

    backend.clearCommands();
    runtime.encodeMotionVectors(backend, {});
    const vfx::test::MockCommand* worldCommand =
        findUniformCommand(backend, findUniform(backend, "u_vfx_world"));
    const vfx::test::MockCommand* previousCommand =
        findUniformCommand(backend, findUniform(backend, "u_vfx_prevWorld"));
    REQUIRE(worldCommand != nullptr);
    REQUIRE(previousCommand != nullptr);
    const std::array<float, 16> initialWorld = readMatrix(*worldCommand, "initial world");
    const std::array<float, 16> initialPrevious = readMatrix(*previousCommand, "initial previous");
    CHECK(initialWorld[12] == doctest::Approx(1.0f));
    CHECK(initialPrevious[12] == doctest::Approx(1.0f));

    vfx::Mat4 moved = vfx::Mat4::identity();
    moved.m[12] = 2.0f;
    effect->setTransform(moved);
    runtime.update(0.0f);
    backend.clearCommands();
    runtime.encodeMotionVectors(backend, {});
    worldCommand = findUniformCommand(backend, findUniform(backend, "u_vfx_world"));
    previousCommand = findUniformCommand(backend, findUniform(backend, "u_vfx_prevWorld"));
    REQUIRE(worldCommand != nullptr);
    REQUIRE(previousCommand != nullptr);
    CHECK(readMatrix(*worldCommand, "moved world")[12] == doctest::Approx(2.0f));
    CHECK(readMatrix(*previousCommand, "moved previous")[12] == doctest::Approx(1.0f));

    vfx::Mat4 movedAgain = vfx::Mat4::identity();
    movedAgain.m[12] = 3.0f;
    effect->setTransform(movedAgain);
    runtime.update(0.0f);
    backend.clearCommands();
    runtime.encodeMotionVectors(backend, {});
    worldCommand = findUniformCommand(backend, findUniform(backend, "u_vfx_world"));
    previousCommand = findUniformCommand(backend, findUniform(backend, "u_vfx_prevWorld"));
    REQUIRE(worldCommand != nullptr);
    REQUIRE(previousCommand != nullptr);
    CHECK(readMatrix(*worldCommand, "next world")[12] == doctest::Approx(3.0f));
    CHECK(readMatrix(*previousCommand, "next previous")[12] == doctest::Approx(2.0f));

    effect->reset();
    vfx::Mat4 afterReset = vfx::Mat4::identity();
    afterReset.m[12] = 4.0f;
    effect->setTransform(afterReset);
    runtime.update(0.0f);
    runtime.encodeSimulation(backend, {});
    backend.clearCommands();
    runtime.encodeMotionVectors(backend, {});
    worldCommand = findUniformCommand(backend, findUniform(backend, "u_vfx_world"));
    previousCommand = findUniformCommand(backend, findUniform(backend, "u_vfx_prevWorld"));
    REQUIRE(worldCommand != nullptr);
    REQUIRE(previousCommand != nullptr);
    CHECK(readMatrix(*worldCommand, "reset world")[12] == doctest::Approx(4.0f));
    CHECK(readMatrix(*previousCommand, "reset previous")[12] == doctest::Approx(4.0f));

    runtime.destroyEffect(effect);
    backend.destroyProgram(motionProgram);
}

TEST_CASE("vfx runtime encodes multiple effect instances with separate transforms") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/curve_visual.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    REQUIRE(effect->supportsEffectInstancing());
    CHECK(effect->instanceCount() == 1u);

    vfx::Mat4 left = vfx::Mat4::identity();
    left.m[12] = -1.0f;
    vfx::Mat4 right = vfx::Mat4::identity();
    right.m[12] = 3.0f;
    const std::array<vfx::Mat4, 2> transforms{left, right};
    effect->setInstanceTransforms(transforms);
    REQUIRE(effect->instanceCount() == 2u);
    const vfx::Bounds leftBounds = effect->instanceBounds(0u);
    const vfx::Bounds rightBounds = effect->instanceBounds(1u);
    REQUIRE(leftBounds.valid);
    REQUIRE(rightBounds.valid);
    CHECK(leftBounds.min.x == doctest::Approx(-2.0f));
    CHECK(leftBounds.max.x == doctest::Approx(0.0f));
    CHECK(rightBounds.min.x == doctest::Approx(2.0f));
    CHECK(rightBounds.max.x == doctest::Approx(4.0f));
    const vfx::Bounds combined = effect->bounds();
    REQUIRE(combined.valid);
    CHECK(combined.min.x == doctest::Approx(-2.0f));
    CHECK(combined.max.x == doctest::Approx(4.0f));
    const vfx::Bounds allVisible = effect->visibleBounds();
    REQUIRE(allVisible.valid);
    CHECK(allVisible.min.x == doctest::Approx(-2.0f));
    CHECK(allVisible.max.x == doctest::Approx(4.0f));

    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    backend.clearCommands();
    runtime.encodeOutput(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);

    const vfx::UniformHandle instanceParams = findUniform(backend, "u_vfxInstanceParams");
    const vfx::test::MockCommand* paramsCommand = findUniformCommand(backend, instanceParams);
    REQUIRE(paramsCommand != nullptr);
    float instanceCount = 0.0f;
    std::memcpy(&instanceCount, paramsCommand->data.data(), sizeof(float));
    CHECK(instanceCount == doctest::Approx(2.0f));
    const vfx::BufferHandle instanceBuffer = bufferByName(backend, "vfx.instance.transforms");
    REQUIRE(instanceBuffer.valid());
    const auto& instanceData = backend.buffers()[instanceBuffer.index].lastUpdateData;
    REQUIRE(instanceData.size() == 96u * sizeof(float));
    float leftX = 0.0f;
    float rightX = 0.0f;
    std::memcpy(&leftX, instanceData.data() + 12u * sizeof(float), sizeof(float));
    std::memcpy(&rightX, instanceData.data() + 44u * sizeof(float), sizeof(float));
    CHECK(leftX == doctest::Approx(-1.0f));
    CHECK(rightX == doctest::Approx(3.0f));
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::SetStorageBuffer) == 4u);
    CHECK(backend.commands().back().type == vfx::test::MockCommand::Type::EndPass);

    effect->setInstanceCulled(0u, true);
    CHECK_FALSE(effect->isCulled());
    const vfx::Bounds rightVisible = effect->visibleBounds();
    REQUIRE(rightVisible.valid);
    CHECK(rightVisible.min.x == doctest::Approx(2.0f));
    CHECK(rightVisible.max.x == doctest::Approx(4.0f));
    backend.clearCommands();
    runtime.encodeOutput(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);
    const vfx::test::MockCommand* fallbackParams = findLastUniformCommand(backend, instanceParams);
    REQUIRE(fallbackParams != nullptr);
    std::memcpy(&instanceCount, fallbackParams->data.data(), sizeof(float));
    CHECK(instanceCount == doctest::Approx(1.0f));
    const vfx::test::MockCommand* fallbackWorld =
        findUniformCommand(backend, findUniform(backend, "u_vfx_world"));
    REQUIRE(fallbackWorld != nullptr);
    float fallbackX = 0.0f;
    std::memcpy(&fallbackX, fallbackWorld->data.data() + 48u, sizeof(float));
    CHECK(fallbackX == doctest::Approx(3.0f));
    const vfx::BufferHandle singleActiveIndirect =
        bufferByName(backend, "vfx.CurveVisual.active.indirect");
    REQUIRE(singleActiveIndirect.valid());
    std::vector<vfx::BufferHandle> singleDrawBuffers;
    vfx::BufferHandle boundStage0{};
    float fillInstanceCount = -1.0f;
    float fillDispatchCount = -1.0f;
    bool sawSingleActiveFill = false;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::SetStorageBuffer && command.stage == 0u) {
            boundStage0 = command.buffer;
        } else if (command.type == vfx::test::MockCommand::Type::SetUniform &&
                   command.uniform == instanceParams) {
            std::memcpy(&fillInstanceCount, command.data.data(), sizeof(float));
        } else if (command.type == vfx::test::MockCommand::Type::Dispatch &&
                   boundStage0.index == singleActiveIndirect.index) {
            sawSingleActiveFill = true;
            fillDispatchCount = fillInstanceCount;
        } else if (command.type == vfx::test::MockCommand::Type::DrawIndirect) {
            singleDrawBuffers.push_back(command.buffer);
        }
    }
    REQUIRE(singleDrawBuffers.size() == 1u);
    CHECK(singleDrawBuffers[0].index == singleActiveIndirect.index);
    CHECK(sawSingleActiveFill);
    CHECK(fillDispatchCount == doctest::Approx(1.0f));
    effect->setInstanceCulled(1u, true);
    CHECK(effect->isCulled());
    CHECK_FALSE(effect->visibleBounds().valid);
    effect->setCulled(false);
    CHECK_FALSE(effect->isCulled());
    const vfx::Bounds visibleAgain = effect->visibleBounds();
    REQUIRE(visibleAgain.valid);
    CHECK(visibleAgain.min.x == doctest::Approx(-2.0f));
    CHECK(visibleAgain.max.x == doctest::Approx(4.0f));

    runtime.destroyEffect(effect);
    CHECK(backend.liveBufferCount() == 0u);
}

TEST_CASE("vfx runtime batches visible instances through active indirection") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/curve_visual.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);

    vfx::Mat4 left = vfx::Mat4::identity();
    left.m[12] = -1.0f;
    vfx::Mat4 middle = vfx::Mat4::identity();
    middle.m[12] = 3.0f;
    vfx::Mat4 right = vfx::Mat4::identity();
    right.m[12] = 30.0f;
    const std::array<vfx::Mat4, 3> transforms{left, middle, right};
    effect->setInstanceTransforms(transforms);
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});

    effect->setInstanceCulled(1u, true);
    backend.clearCommands();
    runtime.encodeOutput(backend, {});
    CHECK(runtime.diagnostics().draws == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);
    const vfx::test::MockCommand* params =
        findLastUniformCommand(backend, findUniform(backend, "u_vfxInstanceParams"));
    REQUIRE(params != nullptr);
    std::array<float, 4> values{};
    std::memcpy(values.data(), params->data.data(), values.size() * sizeof(float));
    CHECK(values[0] == doctest::Approx(3.0f));
    CHECK(values[1] == doctest::Approx(2.0f));
    CHECK(values[2] == doctest::Approx(96.0f));
    CHECK(values[3] == doctest::Approx(1.0f));

    const vfx::BufferHandle activeIndirect = bufferByName(backend, "vfx.CurveVisual.active.indirect");
    REQUIRE(activeIndirect.valid());
    const vfx::BufferHandle baseIndirect = bufferByName(backend, "vfx.CurveVisual.buffer5");
    REQUIRE(baseIndirect.valid());
    std::vector<vfx::BufferHandle> drawBuffers;
    vfx::BufferHandle boundStage0{};
    float fillInstanceCount = -1.0f;
    float fillDispatchCount = -1.0f;
    bool sawActiveFill = false;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::SetStorageBuffer && command.stage == 0u) {
            boundStage0 = command.buffer;
        } else if (command.type == vfx::test::MockCommand::Type::SetUniform &&
                   command.uniform == findUniform(backend, "u_vfxInstanceParams")) {
            std::memcpy(&fillInstanceCount, command.data.data(), sizeof(float));
        } else if (command.type == vfx::test::MockCommand::Type::Dispatch &&
                   boundStage0.index == activeIndirect.index) {
            sawActiveFill = true;
            fillDispatchCount = fillInstanceCount;
        } else if (command.type == vfx::test::MockCommand::Type::DrawIndirect) {
            drawBuffers.push_back(command.buffer);
        }
    }
    REQUIRE(drawBuffers.size() == 1u);
    CHECK(drawBuffers[0].index == activeIndirect.index);
    CHECK(sawActiveFill);
    CHECK(fillDispatchCount == doctest::Approx(2.0f));

    const vfx::BufferHandle instanceBuffer = bufferByName(backend, "vfx.instance.transforms");
    REQUIRE(instanceBuffer.valid());
    const auto& instanceSlot = backend.buffers()[instanceBuffer.index];
    CHECK(instanceSlot.lastUpdateOffset == 96u * sizeof(float));
    CHECK(instanceSlot.lastUpdateSize == 2u * sizeof(std::uint32_t));
    std::array<std::uint32_t, 2> active{};
    std::memcpy(active.data(), instanceSlot.lastUpdateData.data(), instanceSlot.lastUpdateData.size());
    CHECK(active[0] == 0u);
    CHECK(active[1] == 2u);

    effect->setInstanceCulled(1u, false);
    backend.clearCommands();
    runtime.encodeOutput(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);
    const vfx::test::MockCommand* allParams =
        findLastUniformCommand(backend, findUniform(backend, "u_vfxInstanceParams"));
    REQUIRE(allParams != nullptr);
    std::memcpy(values.data(), allParams->data.data(), values.size() * sizeof(float));
    CHECK(values[0] == doctest::Approx(3.0f));
    CHECK(values[1] == doctest::Approx(3.0f));
    CHECK(values[3] == doctest::Approx(0.0f));
    const vfx::test::MockCommand* allDraw =
        findFirstCommand(backend, vfx::test::MockCommand::Type::DrawIndirect);
    REQUIRE(allDraw != nullptr);
    CHECK(allDraw->buffer.index == baseIndirect.index);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 0u);

    runtime.destroyEffect(effect);
    CHECK(backend.liveBufferCount() == 0u);
}

TEST_CASE("vfx runtime splits active instances into fixed-size batches") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/curve_visual.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    vfx::EffectDesc desc;
    desc.instanceBatchLimit = 2u;
    vfx::Effect* effect = runtime.createEffect(*asset, desc, &error);
    INFO(error);
    REQUIRE(effect != nullptr);

    vfx::Mat4 left = vfx::Mat4::identity();
    left.m[12] = -1.0f;
    vfx::Mat4 middle = vfx::Mat4::identity();
    middle.m[12] = 3.0f;
    vfx::Mat4 right = vfx::Mat4::identity();
    right.m[12] = 30.0f;
    const std::array<vfx::Mat4, 3> transforms{left, middle, right};
    effect->setInstanceTransforms(transforms);
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    backend.clearCommands();
    runtime.encodeOutput(backend, {});

    const vfx::BufferHandle firstBatch = bufferByName(backend, "vfx.CurveVisual.active.indirect");
    const vfx::BufferHandle secondBatch =
        bufferByName(backend, "vfx.CurveVisual.active.indirect.1");
    REQUIRE(firstBatch.valid());
    REQUIRE(secondBatch.valid());
    CHECK(runtime.diagnostics().draws == 2u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 2u);

    std::vector<std::array<float, 4>> params;
    std::vector<vfx::BufferHandle> drawBuffers;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::SetUniform &&
            command.uniform == findUniform(backend, "u_vfxInstanceParams")) {
            std::array<float, 4> values{};
            std::memcpy(values.data(), command.data.data(), values.size() * sizeof(float));
            params.push_back(values);
        } else if (command.type == vfx::test::MockCommand::Type::DrawIndirect) {
            drawBuffers.push_back(command.buffer);
        }
    }
    REQUIRE(params.size() >= 2u);
    CHECK(params[params.size() - 2u][0] == doctest::Approx(3.0f));
    CHECK(params[params.size() - 2u][1] == doctest::Approx(2.0f));
    CHECK(params[params.size() - 2u][2] == doctest::Approx(96.0f));
    CHECK(params[params.size() - 2u][3] == doctest::Approx(1.0f));
    CHECK(params.back()[0] == doctest::Approx(3.0f));
    CHECK(params.back()[1] == doctest::Approx(1.0f));
    CHECK(params.back()[2] == doctest::Approx(98.0f));
    CHECK(params.back()[3] == doctest::Approx(1.0f));
    REQUIRE(drawBuffers.size() == 2u);
    CHECK(drawBuffers[0].index == firstBatch.index);
    CHECK(drawBuffers[1].index == secondBatch.index);

    effect->setInstanceCulled(1u, true);
    backend.clearCommands();
    runtime.encodeOutput(backend, {});
    CHECK(runtime.diagnostics().draws == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);
    const vfx::BufferHandle instanceBuffer = bufferByName(backend, "vfx.instance.transforms");
    REQUIRE(instanceBuffer.valid());
    const auto& instanceSlot = backend.buffers()[instanceBuffer.index];
    CHECK(instanceSlot.lastUpdateOffset == 96u * sizeof(float));
    CHECK(instanceSlot.lastUpdateSize == 2u * sizeof(std::uint32_t));
    std::array<std::uint32_t, 2> active{};
    std::memcpy(active.data(), instanceSlot.lastUpdateData.data(), instanceSlot.lastUpdateData.size());
    CHECK(active[0] == 0u);
    CHECK(active[1] == 2u);
    params.clear();
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::SetUniform &&
            command.uniform == findUniform(backend, "u_vfxInstanceParams")) {
            std::array<float, 4> values{};
            std::memcpy(values.data(), command.data.data(), values.size() * sizeof(float));
            params.push_back(values);
        }
    }
    REQUIRE(!params.empty());
    CHECK(params.back()[0] == doctest::Approx(3.0f));
    CHECK(params.back()[1] == doctest::Approx(2.0f));
    CHECK(params.back()[2] == doctest::Approx(96.0f));
    CHECK(params.back()[3] == doctest::Approx(1.0f));

    runtime.destroyEffect(effect);
    CHECK(backend.liveBufferCount() == 0u);
}

TEST_CASE("vfx runtime encodes one transparent instance batch at a time") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/curve_visual.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    vfx::EffectDesc desc;
    desc.instanceBatchLimit = 1u;
    vfx::Effect* effect = runtime.createEffect(*asset, desc, &error);
    INFO(error);
    REQUIRE(effect != nullptr);

    vfx::Mat4 left = vfx::Mat4::identity();
    left.m[12] = -1.0f;
    vfx::Mat4 middle = vfx::Mat4::identity();
    middle.m[12] = 3.0f;
    vfx::Mat4 right = vfx::Mat4::identity();
    right.m[12] = 30.0f;
    const std::array<vfx::Mat4, 3> transforms{left, middle, right};
    effect->setInstanceTransforms(transforms);
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    CHECK(effect->outputBatchCount() == 3u);
    CHECK(effect->outputBatchSize() == 1u);
    CHECK_FALSE(effect->isInstanceCulled(1u));

    const vfx::UniformHandle instanceParams = findUniform(backend, "u_vfxInstanceParams");
    backend.clearCommands();
    runtime.beginOutputFrame();
    runtime.encodeOutputEffectBatch(backend, {}, *effect, 1u);
    CHECK(runtime.diagnostics().draws == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);
    const vfx::test::MockCommand* params = findLastUniformCommand(backend, instanceParams);
    REQUIRE(params != nullptr);
    std::array<float, 4> values{};
    std::memcpy(values.data(), params->data.data(), values.size() * sizeof(float));
    CHECK(values[0] == doctest::Approx(3.0f));
    CHECK(values[1] == doctest::Approx(1.0f));
    CHECK(values[2] == doctest::Approx(97.0f));
    CHECK(values[3] == doctest::Approx(1.0f));
    const vfx::BufferHandle secondBatch =
        bufferByName(backend, "vfx.CurveVisual.active.indirect.1");
    REQUIRE(secondBatch.valid());
    const vfx::test::MockCommand* draw =
        findFirstCommand(backend, vfx::test::MockCommand::Type::DrawIndirect);
    REQUIRE(draw != nullptr);
    CHECK(draw->buffer.index == secondBatch.index);

    effect->setInstanceCulled(1u, true);
    CHECK(effect->isInstanceCulled(1u));
    CHECK(effect->outputBatchCount() == 2u);
    backend.clearCommands();
    runtime.beginOutputFrame();
    runtime.encodeOutputEffectBatch(backend, {}, *effect, 1u);
    CHECK(runtime.diagnostics().draws == 1u);
    params = findLastUniformCommand(backend, instanceParams);
    REQUIRE(params != nullptr);
    std::memcpy(values.data(), params->data.data(), values.size() * sizeof(float));
    CHECK(values[0] == doctest::Approx(3.0f));
    CHECK(values[1] == doctest::Approx(1.0f));
    CHECK(values[2] == doctest::Approx(97.0f));
    CHECK(values[3] == doctest::Approx(1.0f));
    const vfx::BufferHandle instanceBuffer = bufferByName(backend, "vfx.instance.transforms");
    REQUIRE(instanceBuffer.valid());
    const auto& instanceSlot = backend.buffers()[instanceBuffer.index];
    CHECK(instanceSlot.lastUpdateOffset == 96u * sizeof(float));
    CHECK(instanceSlot.lastUpdateSize == 2u * sizeof(std::uint32_t));
    std::array<std::uint32_t, 2> active{};
    std::memcpy(active.data(), instanceSlot.lastUpdateData.data(), instanceSlot.lastUpdateData.size());
    CHECK(active[0] == 0u);
    CHECK(active[1] == 2u);

    runtime.destroyEffect(effect);
    CHECK(backend.liveBufferCount() == 0u);
}

TEST_CASE("vfx runtime batches shadow draws across effect instances") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> loaded =
        runtime.loadAsset(sourcePath() / "tests/assets/curve_visual.vfxb", &error);
    INFO(error);
    REQUIRE(loaded != nullptr);
    vfx::Asset shadowAsset = *loaded;
    for (vfx::AssetProgram& program : shadowAsset.programs) {
        if (program.output) {
            program.castShadows = true;
        }
    }

    const std::array<std::byte, 4> dummyShader{};
    const std::array<vfx::ShaderBlob, 2> blobs{{
        {vfx::ShaderStage::Vertex, vfx::ShaderBackend::Dxbc, dummyShader},
        {vfx::ShaderStage::Fragment, vfx::ShaderBackend::Dxbc, dummyShader},
    }};
    const vfx::ProgramHandle shadowProgram = backend.createProgram(blobs);
    REQUIRE(shadowProgram.valid());
    backend.setOutputProgram(shadowProgram);

    vfx::Effect* effect = runtime.createEffect(shadowAsset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    vfx::Mat4 left = vfx::Mat4::identity();
    left.m[12] = -1.0f;
    vfx::Mat4 right = vfx::Mat4::identity();
    right.m[12] = 3.0f;
    const std::array<vfx::Mat4, 2> transforms{left, right};
    effect->setInstanceTransforms(transforms);
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    backend.clearCommands();
    runtime.encodeShadow(backend, vfx::Mat4::identity());
    CHECK(runtime.diagnostics().shadowDraws == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);
    const vfx::test::MockCommand* params =
        findUniformCommand(backend, findUniform(backend, "u_vfxInstanceParams"));
    REQUIRE(params != nullptr);
    float instanceCount = 0.0f;
    std::memcpy(&instanceCount, params->data.data(), sizeof(float));
    CHECK(instanceCount == doctest::Approx(2.0f));

    effect->setInstanceCulled(0u, true);
    backend.clearCommands();
    runtime.encodeShadow(backend, vfx::Mat4::identity());
    CHECK(runtime.diagnostics().shadowDraws == 2u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);
    const vfx::test::MockCommand* culledParams =
        findUniformCommand(backend, findUniform(backend, "u_vfxInstanceParams"));
    REQUIRE(culledParams != nullptr);
    std::array<float, 4> shadowParams{};
    std::memcpy(shadowParams.data(), culledParams->data.data(), shadowParams.size() * sizeof(float));
    CHECK(shadowParams[0] == doctest::Approx(2.0f));
    CHECK(shadowParams[1] == doctest::Approx(2.0f));
    CHECK(shadowParams[2] == doctest::Approx(0.0f));
    CHECK(shadowParams[3] == doctest::Approx(0.0f));
    const vfx::BufferHandle shadowBaseIndirect = bufferByName(backend, "vfx.CurveVisual.buffer5");
    REQUIRE(shadowBaseIndirect.valid());
    const vfx::test::MockCommand* shadowDraw =
        findFirstCommand(backend, vfx::test::MockCommand::Type::DrawIndirect);
    REQUIRE(shadowDraw != nullptr);
    CHECK(shadowDraw->buffer.index == shadowBaseIndirect.index);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 0u);

    runtime.destroyEffect(effect);
    backend.destroyProgram(shadowProgram);
}

TEST_CASE("vfx runtime splits shadow draws into fixed-size batches") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> loaded =
        runtime.loadAsset(sourcePath() / "tests/assets/curve_visual.vfxb", &error);
    INFO(error);
    REQUIRE(loaded != nullptr);
    vfx::Asset shadowAsset = *loaded;
    for (vfx::AssetProgram& program : shadowAsset.programs) {
        if (program.output) {
            program.castShadows = true;
        }
    }

    const std::array<std::byte, 4> dummyShader{};
    const std::array<vfx::ShaderBlob, 2> blobs{{
        {vfx::ShaderStage::Vertex, vfx::ShaderBackend::Dxbc, dummyShader},
        {vfx::ShaderStage::Fragment, vfx::ShaderBackend::Dxbc, dummyShader},
    }};
    const vfx::ProgramHandle shadowProgram = backend.createProgram(blobs);
    REQUIRE(shadowProgram.valid());
    backend.setOutputProgram(shadowProgram);

    vfx::EffectDesc desc;
    desc.instanceBatchLimit = 2u;
    vfx::Effect* effect = runtime.createEffect(shadowAsset, desc, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    vfx::Mat4 left = vfx::Mat4::identity();
    left.m[12] = -1.0f;
    vfx::Mat4 middle = vfx::Mat4::identity();
    middle.m[12] = 3.0f;
    vfx::Mat4 right = vfx::Mat4::identity();
    right.m[12] = 30.0f;
    const std::array<vfx::Mat4, 3> transforms{left, middle, right};
    effect->setInstanceTransforms(transforms);
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    backend.clearCommands();
    runtime.encodeShadow(backend, vfx::Mat4::identity());

    const vfx::BufferHandle firstBatch = bufferByName(backend, "vfx.CurveVisual.shadow.indirect");
    const vfx::BufferHandle secondBatch =
        bufferByName(backend, "vfx.CurveVisual.shadow.indirect.1");
    REQUIRE(firstBatch.valid());
    REQUIRE(secondBatch.valid());
    CHECK(runtime.diagnostics().shadowDraws == 2u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 2u);

    std::vector<std::array<float, 4>> params;
    std::vector<vfx::BufferHandle> drawBuffers;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::SetUniform &&
            command.uniform == findUniform(backend, "u_vfxInstanceParams")) {
            std::array<float, 4> values{};
            std::memcpy(values.data(), command.data.data(), values.size() * sizeof(float));
            params.push_back(values);
        } else if (command.type == vfx::test::MockCommand::Type::DrawIndirect) {
            drawBuffers.push_back(command.buffer);
        }
    }
    REQUIRE(params.size() >= 2u);
    CHECK(params[params.size() - 2u][0] == doctest::Approx(3.0f));
    CHECK(params[params.size() - 2u][1] == doctest::Approx(2.0f));
    CHECK(params[params.size() - 2u][2] == doctest::Approx(0.0f));
    CHECK(params[params.size() - 2u][3] == doctest::Approx(0.0f));
    CHECK(params.back()[0] == doctest::Approx(3.0f));
    CHECK(params.back()[1] == doctest::Approx(1.0f));
    CHECK(params.back()[2] == doctest::Approx(2.0f));
    CHECK(params.back()[3] == doctest::Approx(0.0f));
    REQUIRE(drawBuffers.size() == 2u);
    CHECK(drawBuffers[0].index == firstBatch.index);
    CHECK(drawBuffers[1].index == secondBatch.index);

    effect->setInstanceCulled(0u, true);
    backend.clearCommands();
    runtime.encodeShadow(backend, vfx::Mat4::identity());
    CHECK(runtime.diagnostics().shadowDraws == 4u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 2u);

    runtime.destroyEffect(effect);
    backend.destroyProgram(shadowProgram);
    CHECK(backend.liveBufferCount() == 0u);
}

TEST_CASE("vfx runtime uses per-instance previous transforms for motion vectors") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/curve_visual.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);

    const std::array<std::byte, 4> dummyShader{};
    const std::array<vfx::ShaderBlob, 2> blobs{{
        {vfx::ShaderStage::Vertex, vfx::ShaderBackend::Dxbc, dummyShader},
        {vfx::ShaderStage::Fragment, vfx::ShaderBackend::Dxbc, dummyShader},
    }};
    const vfx::ProgramHandle motionProgram = backend.createProgram(blobs);
    REQUIRE(motionProgram.valid());
    backend.setOutputProgram(motionProgram);

    vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    vfx::Mat4 firstLeft = vfx::Mat4::identity();
    firstLeft.m[12] = 1.0f;
    vfx::Mat4 firstRight = vfx::Mat4::identity();
    firstRight.m[12] = 4.0f;
    const std::array<vfx::Mat4, 2> firstTransforms{firstLeft, firstRight};
    effect->setInstanceTransforms(firstTransforms);
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});

    vfx::Mat4 secondLeft = vfx::Mat4::identity();
    secondLeft.m[12] = 2.0f;
    vfx::Mat4 secondRight = vfx::Mat4::identity();
    secondRight.m[12] = 5.0f;
    const std::array<vfx::Mat4, 2> secondTransforms{secondLeft, secondRight};
    effect->setInstanceTransforms(secondTransforms);
    runtime.update(0.0f);
    backend.clearCommands();
    runtime.encodeMotionVectors(backend, {});

    const vfx::UniformHandle worldUniform = findUniform(backend, "u_vfx_world");
    const vfx::UniformHandle previousUniform = findUniform(backend, "u_vfx_prevWorld");
    std::vector<std::pair<float, float>> drawTransforms;
    float currentWorld = 0.0f;
    float currentPrevious = 0.0f;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::SetUniform &&
            command.uniform == worldUniform) {
            REQUIRE(command.data.size() == 64u);
            std::memcpy(&currentWorld, command.data.data() + 48u, sizeof(float));
        } else if (command.type == vfx::test::MockCommand::Type::SetUniform &&
                   command.uniform == previousUniform) {
            REQUIRE(command.data.size() == 64u);
            std::memcpy(&currentPrevious, command.data.data() + 48u, sizeof(float));
        } else if (command.type == vfx::test::MockCommand::Type::DrawIndirect) {
            drawTransforms.emplace_back(currentWorld, currentPrevious);
        }
    }
    REQUIRE(drawTransforms.size() == 1u);
    CHECK(drawTransforms[0].first == doctest::Approx(2.0f));
    CHECK(drawTransforms[0].second == doctest::Approx(1.0f));
    const vfx::test::MockCommand* instanceParams =
        findUniformCommand(backend, findUniform(backend, "u_vfxInstanceParams"));
    REQUIRE(instanceParams != nullptr);
    float instanceCount = 0.0f;
    std::memcpy(&instanceCount, instanceParams->data.data(), sizeof(float));
    CHECK(instanceCount == doctest::Approx(2.0f));
    const vfx::BufferHandle instanceBuffer = bufferByName(backend, "vfx.instance.transforms");
    REQUIRE(instanceBuffer.valid());
    const auto& instanceData = backend.buffers()[instanceBuffer.index].lastUpdateData;
    REQUIRE(instanceData.size() == 96u * sizeof(float));
    std::array<float, 4> values{};
    std::memcpy(&values[0], instanceData.data() + 12u * sizeof(float), sizeof(float));
    std::memcpy(&values[1], instanceData.data() + 28u * sizeof(float), sizeof(float));
    std::memcpy(&values[2], instanceData.data() + 44u * sizeof(float), sizeof(float));
    std::memcpy(&values[3], instanceData.data() + 60u * sizeof(float), sizeof(float));
    CHECK(values[0] == doctest::Approx(2.0f));
    CHECK(values[1] == doctest::Approx(1.0f));
    CHECK(values[2] == doctest::Approx(5.0f));
    CHECK(values[3] == doctest::Approx(4.0f));

    effect->setInstanceCulled(0u, true);
    backend.clearCommands();
    runtime.encodeMotionVectors(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);
    currentWorld = 0.0f;
    currentPrevious = 0.0f;
    const vfx::test::MockCommand* fallbackWorld = findUniformCommand(backend, worldUniform);
    const vfx::test::MockCommand* fallbackPrevious = findUniformCommand(backend, previousUniform);
    REQUIRE(fallbackWorld != nullptr);
    REQUIRE(fallbackPrevious != nullptr);
    std::memcpy(&currentWorld, fallbackWorld->data.data() + 48u, sizeof(float));
    std::memcpy(&currentPrevious, fallbackPrevious->data.data() + 48u, sizeof(float));
    CHECK(currentWorld == doctest::Approx(5.0f));
    CHECK(currentPrevious == doctest::Approx(4.0f));

    runtime.destroyEffect(effect);
    backend.destroyProgram(motionProgram);
}

TEST_CASE("vfx runtime rejects instancing for world-space effects") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);
    vfx::Effect* effect = runtime.createEffect(*asset, {});
    REQUIRE(effect != nullptr);
    CHECK_FALSE(effect->supportsEffectInstancing());
    std::string message;
    runtime.setErrorCallback([&](std::string_view text) { message.assign(text); });
    const std::array<vfx::Mat4, 2> transforms{vfx::Mat4::identity(), vfx::Mat4::identity()};
    effect->setInstanceTransforms(transforms);
    CHECK(effect->instanceCount() == 1u);
    CHECK(message.find("requires local-space") != std::string::npos);
    runtime.setErrorCallback({});
    runtime.destroyEffect(effect);
}

TEST_CASE("vfx runtime applies per-output sort direction to motion vectors") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> loaded =
        runtime.loadAsset(sourcePath() / "tests/assets/sort_visual.vfxb", &error);
    INFO(error);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->systems.size() == 1u);
    CHECK(static_cast<int>(loaded->systems[0].sortMode) ==
          static_cast<int>(vfx::SortMode::Distance));
    CHECK(static_cast<int>(loaded->systems[0].sortOrder) ==
          static_cast<int>(vfx::SortOrder::Descending));

    vfx::Asset reversed = *loaded;
    bool foundOutput = false;
    for (vfx::AssetProgram& program : reversed.programs) {
        if (program.output) {
            foundOutput = true;
            program.sortOrder = vfx::SortOrder::Ascending;
        }
    }
    REQUIRE(foundOutput);

    const std::array<std::byte, 4> dummyShader{};
    const std::array<vfx::ShaderBlob, 2> blobs{{
        {vfx::ShaderStage::Vertex, vfx::ShaderBackend::Dxbc, dummyShader},
        {vfx::ShaderStage::Fragment, vfx::ShaderBackend::Dxbc, dummyShader},
    }};
    const vfx::ProgramHandle motionProgram = backend.createProgram(blobs);
    REQUIRE(motionProgram.valid());
    backend.setOutputProgram(motionProgram);

    vfx::Effect* effect = runtime.createEffect(reversed, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    backend.clearCommands();
    runtime.encodeMotionVectors(backend, {});
    const vfx::test::MockCommand* paramsCommand =
        findUniformCommand(backend, findUniform(backend, "u_vfxMotionParams"));
    REQUIRE(paramsCommand != nullptr);
    REQUIRE(paramsCommand->data.size() == 16u);
    std::array<float, 4> params{};
    std::memcpy(params.data(), paramsCommand->data.data(), sizeof(params));
    CHECK(params[2] == doctest::Approx(1.0f));

    runtime.destroyEffect(effect);
    backend.destroyProgram(motionProgram);
}

TEST_CASE("vfx runtime keeps per-output sorted indirection lists") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/sort_outputs.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    REQUIRE(asset->systems.size() == 1u);
    CHECK(static_cast<int>(asset->systems[0].sortMode) ==
          static_cast<int>(vfx::SortMode::Distance));

    vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    backend.clearCommands();
    const std::uint32_t sortPassesBefore = runtime.diagnostics().sortPasses;
    runtime.encodeOutput(backend, {});
    CHECK(runtime.diagnostics().draws == 3u);
    CHECK(runtime.diagnostics().sortPasses == sortPassesBefore + 1u);

    std::vector<std::string> passes;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::BeginPass) {
            passes.push_back(command.passName);
        }
    }
    REQUIRE(passes.size() == 4u);
    CHECK(passes[0] == "VFX SortOutputs Output");
    CHECK(passes[1] == "VFX SortOutputs Output Sort");
    CHECK(passes[2] == "VFX SortOutputs Output");
    CHECK(passes[3] == "VFX SortOutputs Output");

    const vfx::BufferHandle baseIndirection = bufferByName(backend, "vfx.SortOutputs.buffer2");
    const vfx::BufferHandle baseCount = bufferByName(backend, "vfx.SortOutputs.buffer3");
    const vfx::BufferHandle baseIndirect = bufferByName(backend, "vfx.SortOutputs.buffer5");
    const vfx::BufferHandle youngestIndirection =
        bufferByName(backend, "vfx.SortOutputs.sorted.youngest");
    const vfx::BufferHandle youngestKvp =
        bufferByName(backend, "vfx.SortOutputs.sorted.youngest.kvp");
    const vfx::BufferHandle youngestCount =
        bufferByName(backend, "vfx.SortOutputs.sorted.youngest.count");
    const vfx::BufferHandle youngestIndirect =
        bufferByName(backend, "vfx.SortOutputs.sorted.youngest.indirect");
    REQUIRE(baseIndirection.valid());
    REQUIRE(baseCount.valid());
    REQUIRE(baseIndirect.valid());
    REQUIRE(youngestIndirection.valid());
    REQUIRE(youngestKvp.valid());
    REQUIRE(youngestCount.valid());
    REQUIRE(youngestIndirect.valid());
    CHECK(baseIndirection.index != youngestIndirection.index);
    CHECK(baseCount.index != youngestCount.index);
    CHECK(baseIndirect.index != youngestIndirect.index);

    const auto collectDrawStageBuffers = [&](std::uint8_t stage) {
        std::vector<vfx::BufferHandle> result;
        vfx::BufferHandle current{};
        for (const vfx::test::MockCommand& command : backend.commands()) {
            if (command.type == vfx::test::MockCommand::Type::SetStorageBuffer &&
                command.stage == stage) {
                current = command.buffer;
            } else if (command.type == vfx::test::MockCommand::Type::DrawIndirect) {
                result.push_back(current);
            }
        }
        return result;
    };
    const auto collectDrawIndirectBuffers = [&]() {
        std::vector<vfx::BufferHandle> result;
        for (const vfx::test::MockCommand& command : backend.commands()) {
            if (command.type == vfx::test::MockCommand::Type::DrawIndirect) {
                result.push_back(command.buffer);
            }
        }
        return result;
    };
    const std::vector<vfx::BufferHandle> colorIndirections = collectDrawStageBuffers(13u);
    const std::vector<vfx::BufferHandle> colorCounts = collectDrawStageBuffers(14u);
    const std::vector<vfx::BufferHandle> colorIndirects = collectDrawIndirectBuffers();
    REQUIRE(colorIndirections.size() == 3u);
    REQUIRE(colorCounts.size() == 3u);
    REQUIRE(colorIndirects.size() == 3u);
    CHECK(colorIndirections[0].index == baseIndirection.index);
    CHECK(colorIndirections[1].index == youngestIndirection.index);
    CHECK(colorIndirections[2].index == baseIndirection.index);
    CHECK(colorCounts[0].index == baseCount.index);
    CHECK(colorCounts[1].index == youngestCount.index);
    CHECK(colorCounts[2].index == baseCount.index);
    CHECK(colorIndirects[0].index == baseIndirect.index);
    CHECK(colorIndirects[1].index == youngestIndirect.index);
    CHECK(colorIndirects[2].index == baseIndirect.index);

    const std::array<std::byte, 4> dummyShader{};
    const std::array<vfx::ShaderBlob, 2> motionBlobs{{
        {vfx::ShaderStage::Vertex, vfx::ShaderBackend::Dxbc, dummyShader},
        {vfx::ShaderStage::Fragment, vfx::ShaderBackend::Dxbc, dummyShader},
    }};
    const vfx::ProgramHandle motionProgram = backend.createProgram(motionBlobs);
    REQUIRE(motionProgram.valid());
    backend.setOutputProgram(motionProgram);

    backend.clearCommands();
    const std::uint32_t motionSortPassesBefore = runtime.diagnostics().sortPasses;
    runtime.encodeMotionVectors(backend, {});
    CHECK(runtime.diagnostics().sortPasses == motionSortPassesBefore);
    std::uint32_t outputSortPasses = 0;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::BeginPass &&
            command.passName == "VFX SortOutputs Output Sort") {
            ++outputSortPasses;
        }
    }
    CHECK(outputSortPasses == 0u);
    const std::vector<vfx::BufferHandle> motionIndirections = collectDrawStageBuffers(13u);
    const std::vector<vfx::BufferHandle> motionCounts = collectDrawStageBuffers(14u);
    const std::vector<vfx::BufferHandle> motionIndirects = collectDrawIndirectBuffers();
    REQUIRE(motionIndirections.size() == 3u);
    REQUIRE(motionCounts.size() == 3u);
    REQUIRE(motionIndirects.size() == 3u);
    CHECK(motionIndirections[0].index == baseIndirection.index);
    CHECK(motionIndirections[1].index == youngestIndirection.index);
    CHECK(motionIndirections[2].index == baseIndirection.index);
    CHECK(motionCounts[0].index == baseCount.index);
    CHECK(motionCounts[1].index == youngestCount.index);
    CHECK(motionCounts[2].index == baseCount.index);
    CHECK(motionIndirects[0].index == baseIndirect.index);
    CHECK(motionIndirects[1].index == youngestIndirect.index);
    CHECK(motionIndirects[2].index == baseIndirect.index);

    runtime.destroyEffect(effect);
    backend.destroyProgram(motionProgram);
    CHECK(backend.liveBufferCount() == 0u);
}

TEST_CASE("vfx runtime reports device binding limit failures") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> unlit = loadMinimal(runtime, backend);

    backend.setLimits({16, 2});
    std::string error;
    vfx::Effect* effect = runtime.createEffect(*unlit, {}, &error);
    CHECK(effect == nullptr);
    CHECK(error.find("compute binding stage") != std::string::npos);
    CHECK(error.find("exceeds the device storage limit 2") != std::string::npos);
    CHECK(backend.liveBufferCount() == 0u);
    CHECK(backend.liveProgramCount() == 0u);
    CHECK(runtime.diagnostics().effects == 0u);

    backend.setLimits({2, 16});
    const std::shared_ptr<const vfx::Asset> soft =
        runtime.loadAsset(sourcePath() / "tests/assets/soft_visual.vfxb", &error);
    INFO(error);
    REQUIRE(soft != nullptr);
    effect = runtime.createEffect(*soft, {}, &error);
    CHECK(effect == nullptr);
    CHECK(error.find("fragment binding stage") != std::string::npos);
    CHECK(error.find("exceeds the device texture sampler limit 2") != std::string::npos);
    CHECK(backend.liveBufferCount() == 0u);
    CHECK(backend.liveProgramCount() == 0u);
    CHECK(runtime.diagnostics().effects == 0u);
}

TEST_CASE("vfx runtime rejects oversized particle capacity without retaining the asset") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);

    vfx::Asset oversized = *asset;
    oversized.systems[0].capacity = vfx::kMaxParticleCapacity + 1u;
    std::string error;
    vfx::Effect* effect = runtime.createEffect(oversized, {}, &error);
    CHECK(effect == nullptr);
    CHECK(error.find("exceeds the runtime limit") != std::string::npos);
    CHECK(backend.liveBufferCount() == 0u);
    CHECK(backend.liveProgramCount() == 0u);
    CHECK(runtime.diagnostics().effects == 0u);

    effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    runtime.destroyEffect(effect);
    CHECK(backend.liveBufferCount() == 0u);

    vfx::Asset missingShader = *asset;
    for (vfx::AssetProgram& program : missingShader.programs) {
        for (std::vector<std::byte>& blob : program.computeBlobs) {
            blob.clear();
        }
        for (std::vector<std::byte>& blob : program.vertexBlobs) {
            blob.clear();
        }
        for (std::vector<std::byte>& blob : program.fragmentBlobs) {
            blob.clear();
        }
    }
    effect = runtime.createEffect(missingShader, {}, &error);
    CHECK(effect == nullptr);
    CHECK(error.find("missing the selected backend shader") != std::string::npos);
    CHECK(backend.liveBufferCount() == 0u);
    CHECK(runtime.diagnostics().effects == 0u);
}

TEST_CASE("vfx runtime reloads an effect without leaking the previous resources") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);

    vfx::EffectDesc desc;
    desc.playOnAwake = true;
    desc.playRate = 2.0f;
    std::string error;
    vfx::Effect* effect = runtime.createEffect(*asset, desc, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    runtime.update(0.1f);
    CHECK(backend.liveBufferCount() == 9u);
    CHECK(backend.liveProgramCount() == 7u);
    CHECK(backend.liveTextureCount() == 1u);

    vfx::Effect* reloaded = runtime.reloadEffect(effect, *asset, desc, &error);
    INFO(error);
    REQUIRE(reloaded != nullptr);
    CHECK(reloaded != effect);
    CHECK(reloaded->isAlive());
    CHECK(backend.liveBufferCount() == 9u);
    CHECK(backend.liveProgramCount() == 7u);
    CHECK(backend.liveTextureCount() == 1u);
    CHECK(backend.destroyedBufferCount() == 9u);
    CHECK(backend.destroyedProgramCount() == 7u);
    CHECK(backend.destroyedTextureCount() == 1u);
    CHECK(runtime.diagnostics().effects == 1u);

    runtime.destroyEffect(reloaded);
    CHECK(backend.liveBufferCount() == 0u);
    CHECK(backend.liveProgramCount() == 0u);
    CHECK(backend.liveTextureCount() == 0u);
    CHECK(backend.destroyedBufferCount() == 18u);
    CHECK(backend.destroyedProgramCount() == 14u);
    CHECK(backend.destroyedTextureCount() == 2u);
    CHECK(runtime.diagnostics().effects == 0u);
}

TEST_CASE("vfx runtime repeated create and destroy leaves no live resources") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);

    for (std::uint32_t iteration = 0; iteration < 16; ++iteration) {
        vfx::Effect* effect = runtime.createEffect(*asset, {});
        REQUIRE(effect != nullptr);
        CHECK(backend.liveBufferCount() == 9u);
        CHECK(backend.liveProgramCount() == 7u);
        CHECK(backend.liveTextureCount() == 1u);
        runtime.update(0.1f);
        runtime.encodeSimulation(backend, {});
        runtime.encodeOutput(backend, {});
        CHECK(runtime.diagnostics().draws == 1u);
        runtime.destroyEffect(effect);
        CHECK(backend.liveBufferCount() == 0u);
        CHECK(backend.liveProgramCount() == 0u);
        CHECK(backend.liveTextureCount() == 0u);
        CHECK(runtime.diagnostics().effects == 0u);
    }
    CHECK(backend.destroyedBufferCount() == 16u * 9u);
    CHECK(backend.destroyedProgramCount() == 16u * 7u);
    CHECK(backend.destroyedTextureCount() == 16u);
}

TEST_CASE("vfx runtime keeps the old effect when a reload fails") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);

    std::string error;
    vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    const std::size_t liveBuffers = backend.liveBufferCount();
    const std::size_t livePrograms = backend.liveProgramCount();

    vfx::Effect* reloaded = runtime.reloadEffect(
        effect, sourcePath() / "tests/assets/does_not_exist.vfxb", {}, &error);
    CHECK(reloaded == nullptr);
    CHECK_FALSE(error.empty());
    CHECK(effect->isAlive());
    CHECK(backend.liveBufferCount() == liveBuffers);
    CHECK(backend.liveProgramCount() == livePrograms);
    CHECK(runtime.diagnostics().effects == 1u);

    runtime.destroyEffect(effect);
    CHECK(backend.liveBufferCount() == 0u);
    CHECK(backend.liveProgramCount() == 0u);
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
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::SetStorageBuffer) == 4u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::SetTexture) == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::SetState) == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::EndPass) == 1u);
    CHECK(runtime.diagnostics().draws == 1u);
}

TEST_CASE("vfx runtime binds scene depth and soft particle parameters") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/soft_visual.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    runtime.update(0.0f);

    vfx::FrameContext frame;
    frame.screenSize = vfx::Vec2{640.0f, 360.0f};
    frame.sceneDepth = backend.createTexture({1, 1, 1, vfx::TextureUsage::Sampled,
                                              vfx::TextureFormat::D32F, nullptr, 0, "test.depth"});
    REQUIRE(frame.sceneDepth.valid());

    runtime.encodeSimulation(backend, frame);
    backend.clearCommands();
    runtime.encodeOutput(backend, frame);
    REQUIRE(backend.commands().front().type == vfx::test::MockCommand::Type::BeginPass);
    CHECK(backend.commands().front().readsSceneDepth);

    bool depthBound = false;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::SetTexture && command.stage == 4u) {
            CHECK(command.texture == frame.sceneDepth);
            REQUIRE(command.sampler.index < backend.samplers().size());
            CHECK(backend.samplers()[command.sampler.index].name == "s_vfxSceneDepth");
            depthBound = true;
        }
    }
    CHECK(depthBound);

    const vfx::UniformHandle softParams = findUniform(backend, "u_vfxSoftParams");
    REQUIRE(softParams.valid());
    const vfx::test::MockCommand* softCommand = findUniformCommand(backend, softParams);
    REQUIRE(softCommand != nullptr);
    REQUIRE(softCommand->data.size() == sizeof(vfx::Vec4));
    const auto* values = reinterpret_cast<const float*>(softCommand->data.data());
    CHECK(values[0] == doctest::Approx(2.0f));
    CHECK(values[1] == doctest::Approx(1.0f / 640.0f));
    CHECK(values[2] == doctest::Approx(1.0f / 360.0f));
    CHECK(runtime.diagnostics().draws == 1u);
    runtime.destroyEffect(effect);
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

TEST_CASE("vfx runtime dispatches bitonic pre-pass and merge sort passes") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/reference_sorted_8192.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);

    runtime.update(0.1f);
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 9u);
    CHECK(runtime.diagnostics().sortPasses == 3u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::SetStorageBuffer) == 27u);

    bool foundChunkBase = false;
    const vfx::UniformHandle ids = findUniform(backend, "u_vfx_ids");
    REQUIRE(ids.valid());
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::SetUniform && command.uniform == ids) {
            REQUIRE(command.data.size() == sizeof(vfx::Vec4));
            const auto* values = reinterpret_cast<const float*>(command.data.data());
            foundChunkBase = foundChunkBase || values[2] == doctest::Approx(1.0f);
        }
    }
    CHECK(foundChunkBase);

    backend.clearCommands();
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 8u);
    CHECK(runtime.diagnostics().sortPasses == 3u);
    runtime.destroyEffect(effect);
}

TEST_CASE("vfx runtime reports phase timings and frame totals") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);
    vfx::Effect* effect = runtime.createEffect(*asset, {});
    REQUIRE(effect != nullptr);

    runtime.update(0.1f);
    CHECK(runtime.diagnostics().updateMs >= 0.0f);
    CHECK(runtime.diagnostics().simulationMs == doctest::Approx(0.0f));
    CHECK(runtime.diagnostics().outputMs == doctest::Approx(0.0f));

    runtime.encodeSimulation(backend, {});
    const vfx::Diagnostics afterSimulation = runtime.diagnostics();
    CHECK(afterSimulation.simulationMs >= 0.0f);
    CHECK(afterSimulation.outputMs == doctest::Approx(0.0f));
    CHECK(afterSimulation.encodeMs == doctest::Approx(afterSimulation.simulationMs));
    CHECK(afterSimulation.totalMs ==
          doctest::Approx(afterSimulation.updateMs + afterSimulation.simulationMs));

    runtime.encodeOutput(backend, {});
    const vfx::Diagnostics afterOutput = runtime.diagnostics();
    CHECK(afterOutput.outputMs >= 0.0f);
    CHECK(afterOutput.encodeMs ==
          doctest::Approx(afterOutput.simulationMs + afterOutput.outputMs));
    CHECK(afterOutput.totalMs == doctest::Approx(afterOutput.updateMs + afterOutput.encodeMs));

    runtime.destroyEffect(effect);
}

TEST_CASE("vfx runtime applies automatic GPU bounds readback") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/auto_bounds.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    REQUIRE(asset->systems.size() == 1u);
    CHECK(asset->systems[0].boundsMode == vfx::BoundsMode::Automatic);
    CHECK(asset->systems[0].boundsPadding.x == doctest::Approx(0.25f));
    CHECK(asset->systems[0].boundsPadding.y == doctest::Approx(0.5f));
    CHECK(asset->systems[0].boundsPadding.z == doctest::Approx(0.75f));

    vfx::EffectDesc desc;
    desc.transform.m[12] = 10.0f;
    vfx::Effect* effect = runtime.createEffect(*asset, desc, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    const vfx::BufferHandle boundsBuffer = bufferByName(backend, "vfx.AutoBounds.buffer6");
    REQUIRE(boundsBuffer.valid());

    const auto flip = [](float value) {
        std::uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(bits));
        const std::uint32_t mask =
            static_cast<std::uint32_t>(-static_cast<std::int32_t>(bits >> 31u)) | 0x80000000u;
        return bits ^ mask;
    };
    const float localMin[3] = {-1.25f, -0.5f, -2.0f};
    const float localMax[3] = {0.75f, 0.5f, 1.0f};
    std::array<std::uint32_t, 6> words = {
        flip(localMin[0]), flip(localMin[1]), flip(localMin[2]),
        flip(localMax[0]), flip(localMax[1]), flip(localMax[2]),
    };
    backend.setReadbackData(
        boundsBuffer,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(words.data()),
                                   words.size() * sizeof(std::uint32_t)));

    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    const vfx::UniformHandle boundsParamsUniform = findUniform(backend, "u_vfxBoundsParams");
    const vfx::UniformHandle boundsRightUniform = findUniform(backend, "u_vfxBoundsRight");
    const vfx::UniformHandle boundsUpUniform = findUniform(backend, "u_vfxBoundsUp");
    const vfx::test::MockCommand* boundsParams = findUniformCommand(backend, boundsParamsUniform);
    const vfx::test::MockCommand* boundsRight = findUniformCommand(backend, boundsRightUniform);
    const vfx::test::MockCommand* boundsUp = findUniformCommand(backend, boundsUpUniform);
    REQUIRE(boundsParams != nullptr);
    REQUIRE(boundsRight != nullptr);
    REQUIRE(boundsUp != nullptr);
    std::array<float, 4> boundsParamsValue{};
    std::array<float, 4> boundsRightValue{};
    std::array<float, 4> boundsUpValue{};
    std::memcpy(boundsParamsValue.data(), boundsParams->data.data(), sizeof(boundsParamsValue));
    std::memcpy(boundsRightValue.data(), boundsRight->data.data(), sizeof(boundsRightValue));
    std::memcpy(boundsUpValue.data(), boundsUp->data.data(), sizeof(boundsUpValue));
    CHECK(boundsParamsValue[0] == doctest::Approx(1.0f));
    CHECK(boundsRightValue[0] == doctest::Approx(1.0f));
    CHECK(boundsRightValue[1] == doctest::Approx(0.0f));
    CHECK(boundsUpValue[0] == doctest::Approx(0.0f));
    CHECK(boundsUpValue[1] == doctest::Approx(1.0f));
    runtime.update(0.1f);
    const vfx::Bounds bounds = effect->bounds();
    CHECK(bounds.valid);
    CHECK(bounds.min.x == doctest::Approx(8.5f));
    CHECK(bounds.min.y == doctest::Approx(-1.0f));
    CHECK(bounds.min.z == doctest::Approx(-2.75f));
    CHECK(bounds.max.x == doctest::Approx(11.0f));
    CHECK(bounds.max.y == doctest::Approx(1.0f));
    CHECK(bounds.max.z == doctest::Approx(1.75f));

    runtime.destroyEffect(effect);
}

TEST_CASE("vfx runtime culls simulation and output") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);

    vfx::Asset culledAsset = *asset;
    culledAsset.cullingFlags = vfx::kCullSimulation;
    std::string error;
    vfx::Effect* effect = runtime.createEffect(culledAsset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    effect->setCulled(true);
    CHECK(effect->isCulled());
    runtime.update(0.1f);
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 0u);
    runtime.encodeOutput(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 0u);

    effect->setCulled(false);
    runtime.update(0.1f);
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) > 0u);
    runtime.encodeOutput(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);
    runtime.destroyEffect(effect);

    vfx::Effect* drawCulled = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(drawCulled != nullptr);
    drawCulled->setCulled(true);
    runtime.update(0.1f);
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) > 0u);
    runtime.encodeOutput(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 0u);
    runtime.destroyEffect(drawCulled);
}

TEST_CASE("vfx runtime emits deterministic dispatch order and buffer bindings") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);
    vfx::Effect* effect = runtime.createEffect(*asset, {});
    REQUIRE(effect != nullptr);
    const std::array<const char*, 9> names = {
        "vfx.Sparks.buffer0", "vfx.Sparks.buffer1", "vfx.Sparks.buffer2", "vfx.Sparks.buffer3",
        "vfx.Sparks.buffer4", "vfx.Sparks.buffer5", "vfx.Sparks.buffer6", "vfx.Sparks.buffer7",
        "vfx.Sparks.buffer8",
    };
    std::array<vfx::BufferHandle, 9> buffers{};
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        buffers[i] = bufferByName(backend, names[i]);
        REQUIRE(buffers[i].valid());
    }
    using Binding = std::pair<std::uint8_t, vfx::BufferHandle>;
    const std::vector<std::vector<Binding>> resetSequence = {
        {{0, buffers[4]}, {1, buffers[3]}, {2, buffers[7]}},
        {{0, buffers[1]}, {1, buffers[0]}, {2, buffers[2]}},
        {{0, buffers[0]}, {1, buffers[1]}, {2, buffers[4]}},
        {{0, buffers[0]}, {1, buffers[1]}},
        {{0, buffers[0]}, {1, buffers[7]}, {2, buffers[2]}, {3, buffers[3]}},
        {{0, buffers[5]}, {1, buffers[3]}, {2, buffers[7]}},
    };
    const std::vector<std::vector<Binding>> continuousSequence = {
        {{0, buffers[4]}, {1, buffers[3]}, {2, buffers[7]}},
        {{0, buffers[0]}, {1, buffers[1]}, {2, buffers[4]}},
        {{0, buffers[0]}, {1, buffers[1]}},
        {{0, buffers[0]}, {1, buffers[7]}, {2, buffers[2]}, {3, buffers[3]}},
        {{0, buffers[5]}, {1, buffers[3]}, {2, buffers[7]}},
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
              (reset ? 18u : 15u));
        CHECK(countCommands(backend, vfx::test::MockCommand::Type::BeginPass) == 1u);
        CHECK(countCommands(backend, vfx::test::MockCommand::Type::EndPass) == 1u);
        CHECK(countCommands(backend, vfx::test::MockCommand::Type::SetUniform) == 5u);
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
    REQUIRE(backend.commands().size() == 14u);
    CHECK(backend.commands()[0].type == vfx::test::MockCommand::Type::BeginPass);
    CHECK(backend.commands()[1].type == vfx::test::MockCommand::Type::SetUniform);
    CHECK(backend.commands()[2].type == vfx::test::MockCommand::Type::SetUniform);
    CHECK(backend.commands()[3].type == vfx::test::MockCommand::Type::SetUniform);
    CHECK(backend.commands()[4].type == vfx::test::MockCommand::Type::SetUniform);
    CHECK(backend.commands()[5].type == vfx::test::MockCommand::Type::SetUniform);
    CHECK(backend.commands()[6].type == vfx::test::MockCommand::Type::SetStorageBuffer);
    CHECK(backend.commands()[6].stage == 12u);
    CHECK(backend.commands()[6].buffer == buffers[0]);
    CHECK(backend.commands()[7].type == vfx::test::MockCommand::Type::SetStorageBuffer);
    CHECK(backend.commands()[7].stage == 13u);
    CHECK(backend.commands()[7].buffer == buffers[2]);
    CHECK(backend.commands()[8].type == vfx::test::MockCommand::Type::SetStorageBuffer);
    CHECK(backend.commands()[8].stage == 14u);
    CHECK(backend.commands()[8].buffer == buffers[3]);
    CHECK(backend.commands()[9].type == vfx::test::MockCommand::Type::SetStorageBuffer);
    CHECK(backend.commands()[9].stage == 15u);
    CHECK(backend.commands()[9].buffer == buffers[0]);
    CHECK(backend.commands()[10].type == vfx::test::MockCommand::Type::SetTexture);
    CHECK(backend.commands()[11].type == vfx::test::MockCommand::Type::SetState);
    CHECK(backend.commands()[12].type == vfx::test::MockCommand::Type::DrawIndirect);
    CHECK(backend.commands()[12].buffer == buffers[5]);
    CHECK(backend.commands()[13].type == vfx::test::MockCommand::Type::EndPass);
}

TEST_CASE("vfx runtime applies properties, pause, stop and restart") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    const std::shared_ptr<const vfx::Asset> asset = loadMinimal(runtime, backend);
    vfx::Effect* effect = runtime.createEffect(*asset, {});
    REQUIRE(effect != nullptr);
    const vfx::UniformHandle params = findUniform(backend, "u_vfx_params");
    REQUIRE(params.valid());

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

    effect->advanceOneFrame();
    backend.clearCommands();
    runtime.update(0.0f);
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 5u);
    const vfx::test::MockCommand* stepParams = findUniformCommand(backend, params);
    REQUIRE(stepParams != nullptr);
    const auto* stepValues = reinterpret_cast<const float*>(stepParams->data.data());
    CHECK(stepValues[1] == doctest::Approx(1.0f / 60.0f));
    backend.clearCommands();
    runtime.encodeOutput(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::DrawIndirect) == 1u);

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
    bool curveStage = false;
    bool gradientStage = false;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type != vfx::test::MockCommand::Type::SetTexture) {
            continue;
        }
        curveStage = curveStage || command.stage == 9u;
        gradientStage = gradientStage || command.stage == 10u;
    }
    CHECK(curveStage);
    CHECK(gradientStage);
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
        if (command.type == vfx::test::MockCommand::Type::SetTexture && command.stage == 0u) {
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

TEST_CASE("vfx runtime schedules GPU event reset, prefix sum and consumer init") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/vfx_event.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    CHECK(backend.liveBufferCount() == 19u);
    CHECK(backend.liveProgramCount() == 16u);
    const vfx::BufferHandle eventBuffer = bufferByName(backend, "vfx.events");
    REQUIRE(eventBuffer.valid());
    REQUIRE(backend.buffers()[eventBuffer.index].desc.size == 4372u);
    CHECK(backend.buffers()[eventBuffer.index].desc.usage == vfx::BufferUsage::Storage);

    runtime.update(0.1f);
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::BeginPass) == 4u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::EndPass) == 4u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 14u);
    CHECK(runtime.diagnostics().dispatches == 14u);
    CHECK(runtime.diagnostics().eventCount == 1u);

    std::vector<std::string> passes;
    bool sawEventBind = false;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::BeginPass) {
            passes.push_back(command.passName);
        }
        if (command.type == vfx::test::MockCommand::Type::SetStorageBuffer &&
            command.buffer.index == eventBuffer.index) {
            sawEventBind = true;
        }
    }
    REQUIRE(passes.size() == 4u);
    CHECK(passes[0] == "VFX Event Reset");
    CHECK(passes[1] == "VFX Emitter Simulation");
    CHECK(passes[2] == "VFX Event Prefix Sum");
    CHECK(passes[3] == "VFX Receiver Simulation");
    CHECK(sawEventBind);

    runtime.destroyEffect(effect);
    CHECK(backend.liveBufferCount() == 0u);
    CHECK(backend.liveProgramCount() == 0u);
}

TEST_CASE("vfx runtime sorts output draws by asset priority") {
    const std::vector<std::byte> bytes =
        binaryWithOutputPriorities(sourcePath() / "tests/assets/vfx_event.vfxb", {10, -10});
    REQUIRE_FALSE(bytes.empty());
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset = vfx::parseAssetBinary(bytes, error);
    INFO(error);
    REQUIRE(asset != nullptr);

    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    runtime.update(0.1f);
    runtime.encodeSimulation(backend, {});
    backend.clearCommands();
    runtime.encodeOutput(backend, {});
    CHECK(runtime.diagnostics().draws == 2u);

    std::vector<std::string> passes;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::BeginPass) {
            passes.push_back(command.passName);
        }
    }
    REQUIRE(passes.size() == 2u);
    CHECK(passes[0] == "VFX Receiver Output");
    CHECK(passes[1] == "VFX Emitter Output");

    runtime.destroyEffect(effect);
}

TEST_CASE("vfx runtime queues CPU events and schedules the upload copy") {
    vfx::test::MockBackend backend;
    vfx::Runtime runtime(backend, &backend);
    std::string error;
    const std::shared_ptr<const vfx::Asset> asset =
        runtime.loadAsset(sourcePath() / "tests/assets/vfx_cpu_event.vfxb", &error);
    INFO(error);
    REQUIRE(asset != nullptr);
    vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
    INFO(error);
    REQUIRE(effect != nullptr);
    CHECK(backend.liveBufferCount() == 11u);
    CHECK(backend.liveProgramCount() == 10u);
    CHECK(bufferByName(backend, "vfx.cpu.events").valid());

    std::string eventError;
    runtime.setErrorCallback([&](std::string_view message) { eventError.assign(message); });
    vfx::EventAttribute invalidPosition;
    invalidPosition.name = "position";
    invalidPosition.value.type = vfx::PropertyType::Float;
    invalidPosition.value.f = {42.0f, 0.0f, 0.0f, 0.0f};
    const std::array<vfx::EventAttribute, 1> invalid = {invalidPosition};
    effect->sendEvent("hit", invalid);
    CHECK(eventError.find("incompatible type") != std::string::npos);
    eventError.clear();
    effect->sendEvent("missing", {});
    CHECK(eventError.find("not declared") != std::string::npos);
    runtime.setErrorCallback({});

    vfx::EventAttribute position;
    position.name = "position";
    position.value.type = vfx::PropertyType::Vector3;
    position.value.f = {1.0f, 2.0f, 3.0f, 0.0f};
    const std::array<vfx::EventAttribute, 1> attributes = {position};
    effect->sendEvent("hit", attributes);
    effect->sendEvent("hit", attributes);

    runtime.update(0.1f);
    backend.clearCommands();
    runtime.encodeSimulation(backend, {});
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::BeginPass) == 4u);
    CHECK(countCommands(backend, vfx::test::MockCommand::Type::Dispatch) == 9u);
    CHECK(runtime.diagnostics().eventCount == 1u);

    std::vector<std::string> passes;
    for (const vfx::test::MockCommand& command : backend.commands()) {
        if (command.type == vfx::test::MockCommand::Type::BeginPass) {
            passes.push_back(command.passName);
        }
    }
    REQUIRE(passes.size() == 4u);
    CHECK(passes[0] == "VFX Event Reset");
    CHECK(passes[1] == "VFX CPU Event Upload");
    CHECK(passes[2] == "VFX Event Prefix Sum");
    CHECK(passes[3] == "VFX Receiver Simulation");

    runtime.destroyEffect(effect);
    CHECK(backend.liveBufferCount() == 0u);
    CHECK(backend.liveProgramCount() == 0u);
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

