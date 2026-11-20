#include "support/mock_backend.h"

#include "vfx/command_sink.h"
#include "vfx/device.h"
#include "vfx/shading.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("mock backend records the command stream in order") {
    vfx::test::MockBackend backend;

    const vfx::BufferHandle attributes =
        backend.createBuffer({vfx::BufferUsage::StorageVertex, 256, 16, nullptr, "attributes"});
    const vfx::BufferHandle indirectArgs =
        backend.createBuffer({vfx::BufferUsage::Indirect, 32, 32, nullptr, "indirect"});
    const vfx::TextureHandle texture = backend.createTexture({4, 4, 1, vfx::TextureUsage::Sampled,
                                                              vfx::TextureFormat::RGBA16F, nullptr, 0, "lut"});
    const std::byte shaderData[4] = {};
    const vfx::ShaderBlob blobs[] = {
        {vfx::ShaderStage::Vertex, vfx::ShaderBackend::Dxbc, std::span<const std::byte>(shaderData)},
        {vfx::ShaderStage::Fragment, vfx::ShaderBackend::Dxbc, std::span<const std::byte>(shaderData)},
    };
    const vfx::ProgramHandle program = backend.createProgram(blobs);
    const vfx::UniformHandle params = backend.findUniform("u_vfxParams", vfx::UniformType::Vec4);
    const vfx::SamplerHandle sampler = backend.findSampler("s_vfxMain");

    REQUIRE(attributes.valid());
    REQUIRE(indirectArgs.valid());
    REQUIRE(texture.valid());
    REQUIRE(program.valid());
    REQUIRE(params.valid());
    REQUIRE(sampler.valid());

    const vfx::Vec4 value{1.0f, 2.0f, 3.0f, 4.0f};
    vfx::DrawState drawState;
    drawState.depthWrite = true;
    drawState.blend = vfx::BlendMode::Additive;

    backend.beginPass({"VFX Simulation", true, false, false});
    backend.setStorageBuffer(0, attributes, vfx::Access::ReadWrite);
    backend.setTexture(1, sampler, texture);
    backend.setUniform(params, &value, 1);
    backend.setState(drawState);
    backend.dispatch(program, 8, 4, 1);
    backend.drawIndirect(program, indirectArgs, 0, 1);
    backend.endPass();

    const auto& commands = backend.commands();
    REQUIRE(commands.size() == 8);
    CHECK(commands[0].type == vfx::test::MockCommand::Type::BeginPass);
    CHECK(commands[0].passName == "VFX Simulation");
    CHECK(commands[0].simulation);
    CHECK(commands[1].type == vfx::test::MockCommand::Type::SetStorageBuffer);
    CHECK(commands[1].stage == 0);
    CHECK(commands[1].buffer == attributes);
    CHECK(commands[1].access == vfx::Access::ReadWrite);
    CHECK(commands[2].type == vfx::test::MockCommand::Type::SetTexture);
    CHECK(commands[2].stage == 1);
    CHECK(commands[2].sampler == sampler);
    CHECK(commands[2].texture == texture);
    CHECK(commands[3].type == vfx::test::MockCommand::Type::SetUniform);
    CHECK(commands[3].uniform == params);
    CHECK(commands[3].count == 1);
    REQUIRE(commands[3].data.size() == sizeof(value));
    CHECK(std::memcmp(commands[3].data.data(), &value, sizeof(value)) == 0);
    CHECK(commands[4].type == vfx::test::MockCommand::Type::SetState);
    CHECK(commands[4].state.depthWrite);
    CHECK(commands[4].state.blend == vfx::BlendMode::Additive);
    CHECK(commands[5].type == vfx::test::MockCommand::Type::Dispatch);
    CHECK(commands[5].program == program);
    CHECK(commands[5].x == 8);
    CHECK(commands[5].y == 4);
    CHECK(commands[5].z == 1);
    CHECK(commands[6].type == vfx::test::MockCommand::Type::DrawIndirect);
    CHECK(commands[6].program == program);
    CHECK(commands[6].buffer == indirectArgs);
    CHECK(commands[6].num == 1);
    CHECK(commands[7].type == vfx::test::MockCommand::Type::EndPass);

    backend.bindLighting(backend, {}, {});
    CHECK(backend.lightingBinds() == 1);
    CHECK_FALSE(backend.resolveProgram({"unlit", false}, vfx::ShaderBackend::Dxbc).valid());
}

TEST_CASE("mock backend returns stable cached handles") {
    vfx::test::MockBackend backend;

    const vfx::UniformHandle first = backend.findUniform("u_vfxParams", vfx::UniformType::Vec4);
    const vfx::UniformHandle second = backend.findUniform("u_vfxParams", vfx::UniformType::Vec4);
    const vfx::SamplerHandle firstSampler = backend.findSampler("s_vfxMain");
    const vfx::SamplerHandle secondSampler = backend.findSampler("s_vfxMain");

    CHECK(first == second);
    CHECK(firstSampler == secondSampler);
    CHECK(backend.uniforms().size() == 1);
    CHECK(backend.samplers().size() == 1);
}

TEST_CASE("mock backend tracks live and destroyed resources") {
    vfx::test::MockBackend backend;

    const std::byte shaderData[4] = {};
    const vfx::ShaderBlob blob{vfx::ShaderStage::Compute, vfx::ShaderBackend::Dxbc,
                               std::span<const std::byte>(shaderData)};

    for (std::uint32_t index = 0; index < 100; ++index) {
        const vfx::BufferHandle buffer =
            backend.createBuffer({vfx::BufferUsage::Storage, 64, 4, nullptr, "buffer"});
        const vfx::ProgramHandle program = backend.createProgram(std::span<const vfx::ShaderBlob>(&blob, 1));
        REQUIRE(buffer.valid());
        REQUIRE(program.valid());
    }

    CHECK(backend.liveBufferCount() == 100);
    CHECK(backend.liveProgramCount() == 100);
    CHECK(backend.buffers().size() == 100);
    CHECK(backend.programs().size() == 100);

    for (std::uint32_t index = 0; index < 100; ++index) {
        backend.destroyBuffer(vfx::BufferHandle{index});
        backend.destroyProgram(vfx::ProgramHandle{index});
    }

    CHECK(backend.liveBufferCount() == 0);
    CHECK(backend.liveProgramCount() == 0);
    CHECK(backend.destroyedBufferCount() == 100);
    CHECK(backend.destroyedProgramCount() == 100);
}

TEST_CASE("public vfx backend headers stay engine independent") {
    const std::filesystem::path includeRoot = std::filesystem::path(VFX_SOURCE_DIR) / "include" / "vfx";
    REQUIRE(std::filesystem::is_directory(includeRoot));

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(includeRoot)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".h") {
            continue;
        }
        const std::string text = readTextFile(entry.path());
        CAPTURE(entry.path().string());
        CHECK(text.find("bgfx::") == std::string::npos);
        CHECK(text.find("hbg::") == std::string::npos);
        CHECK(text.find("RenderGraph") == std::string::npos);
    }
}

TEST_CASE("mat4 identity uses column major storage") {
    const vfx::Mat4 identity = vfx::Mat4::identity();
    CHECK(identity.m[0] == 1.0f);
    CHECK(identity.m[5] == 1.0f);
    CHECK(identity.m[10] == 1.0f);
    CHECK(identity.m[15] == 1.0f);
    CHECK(identity.m[1] == 0.0f);
    CHECK(identity.data()[0] == 1.0f);
}
