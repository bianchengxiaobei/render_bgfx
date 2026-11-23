#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "render/vfx/hbg_vfx_backend.h"

#include "gfx/encoder.h"
#include "gfx/resource_manager.h"
#include "render/passes/forward_lighting.h"

#include "vfx/runtime.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string_view>
#include <vector>

namespace {

bool initNoopRenderer() {
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    init.vendorId = BGFX_PCI_ID_NONE;
    init.swapChain.width = 1;
    init.swapChain.height = 1;
    return bgfx::init(init);
}

std::vector<std::byte> loadShader(std::string_view file) {
    const std::filesystem::path path = std::filesystem::path(VFX_SOURCE_DIR) / "tests" / "data" / file;
    std::ifstream stream(path, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> result(bytes.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

std::vector<std::byte> loadBakedAsset() {
    const std::filesystem::path path =
        std::filesystem::path(VFX_SOURCE_DIR) / "tests" / "assets" / "minimal_unlit.vfxb";
    std::ifstream stream(path, std::ios::binary);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> result(bytes.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

std::uint32_t readU32(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[offset + i]) << (i * 8u);
    }
    return value;
}

} // namespace

TEST_CASE("forward lighting bindings preserve the VFX stage budget") {
    CHECK(hbg::kForwardLightingLightDataStage == 5u);
    CHECK(hbg::kForwardLightingDirectionalShadowStage == 6u);
    CHECK(hbg::kForwardLightingLightListStage == 7u);
    CHECK(hbg::kForwardLightingShadowContextStage == 8u);
    CHECK(hbg::kForwardLightingPunctualShadowStage == 9u);
    CHECK(hbg::kForwardLightingEnvironmentStage == 10u);
    CHECK(hbg::kForwardLightingContactShadowStage == 11u);
    CHECK(hbg::kForwardLightingProbeArrayStage == 14u);
    CHECK(hbg::kForwardLightingCookieAtlasStage == 15u);
    CHECK(hbg::kForwardLightingContactShadowStage != 12u);
    CHECK(hbg::kForwardLightingContactShadowStage != 13u);
}

TEST_CASE("hbg vfx backend creates and destroys buffers and programs without leaks") {
    REQUIRE(initNoopRenderer());

    {
        hbg::gfx::ResourceManager resources;
        resources.init();
        {
            hbg::vfxbridge::HbgVfxBackend backend(resources);
            const std::vector<std::byte> shaderData = loadShader("minimal_compute_dxbc.bin");
            REQUIRE_FALSE(shaderData.empty());
            std::array<vfx::ShaderBlob, 1> blobs{};
            blobs[0] = {vfx::ShaderStage::Compute, backend.backend(), shaderData};

            std::vector<vfx::BufferHandle> buffers;
            std::vector<vfx::ProgramHandle> programs;
            buffers.reserve(100);
            programs.reserve(100);

            for (std::uint32_t index = 0; index < 100; ++index) {
                vfx::BufferDesc desc;
                desc.usage = index % 2 == 0 ? vfx::BufferUsage::Storage : vfx::BufferUsage::StorageVertex;
                desc.size = 256;
                desc.stride = 16;
                desc.name = "backend test buffer";
                buffers.push_back(backend.createBuffer(desc));
                programs.push_back(backend.createProgram(blobs));
                REQUIRE(buffers.back().valid());
                REQUIRE(programs.back().valid());
            }

            CHECK(backend.liveBufferCount() == 100);
            CHECK(backend.liveProgramCount() == 100);
            CHECK(resources.liveResourceCount() == 200);

            for (const vfx::BufferHandle buffer : buffers) {
                backend.destroyBuffer(buffer);
            }
            for (const vfx::ProgramHandle program : programs) {
                backend.destroyProgram(program);
            }

            CHECK(backend.liveBufferCount() == 0);
            CHECK(backend.liveProgramCount() == 0);
            CHECK(resources.liveResourceCount() == 0);
        }
        resources.shutdown();
        CHECK_FALSE(resources.hadLeaksAtShutdown());
    }

    bgfx::shutdown();
}

TEST_CASE("hbg vfx backend creates programs from a baked asset") {
    REQUIRE(initNoopRenderer());

    {
        hbg::gfx::ResourceManager resources;
        resources.init();
        {
            hbg::vfxbridge::HbgVfxBackend backend(resources);
            const std::vector<std::byte> asset = loadBakedAsset();
            REQUIRE(asset.size() > 64u);
            REQUIRE(asset[0] == std::byte{'V'});
            const std::uint32_t sectionCount = readU32(asset, 12);
            REQUIRE(sectionCount >= 9u);

            std::size_t kernelsOffset = 0;
            std::size_t kernelsSize = 0;
            for (std::uint32_t i = 0; i < sectionCount; ++i) {
                const std::size_t table = 64u + i * 16u;
                if (readU32(asset, table) == 6u) {
                    kernelsOffset = readU32(asset, table + 4u);
                    kernelsSize = readU32(asset, table + 8u);
                    break;
                }
            }
            REQUIRE(kernelsOffset != 0u);
            const std::uint32_t kernelCount = readU32(asset, kernelsOffset);
            REQUIRE(kernelCount == 16u);

            std::map<std::uint32_t, std::vector<vfx::ShaderBlob>> byProgram;
            for (std::uint32_t i = 0; i < kernelCount; ++i) {
                const std::size_t record = kernelsOffset + 4u + i * 44u;
                const auto shaderStage = static_cast<vfx::ShaderStage>(static_cast<std::uint8_t>(asset[record + 5u]));
                const auto shaderBackend =
                    static_cast<vfx::ShaderBackend>(static_cast<std::uint8_t>(asset[record + 6u]));
                const std::uint32_t blobOffset = readU32(asset, record + 28u);
                const std::uint32_t blobSize = readU32(asset, record + 32u);
                const std::uint32_t program = readU32(asset, record + 36u);
                if (shaderBackend != backend.backend()) {
                    continue;
                }
                REQUIRE(blobOffset + blobSize <= kernelsSize);
                const std::span<const std::byte> data(asset.data() + kernelsOffset + blobOffset, blobSize);
                byProgram[program].push_back(vfx::ShaderBlob{shaderStage, shaderBackend, data});
            }

            REQUIRE(byProgram.size() == 7u);
            std::vector<vfx::ProgramHandle> programs;
            for (const auto& [program, blobs] : byProgram) {
                (void)program;
                const vfx::ProgramHandle handle = backend.createProgram(blobs);
                REQUIRE(handle.valid());
                programs.push_back(handle);
            }
            CHECK(backend.liveProgramCount() == 7u);
            for (const vfx::ProgramHandle handle : programs) {
                backend.destroyProgram(handle);
            }
            CHECK(backend.liveProgramCount() == 0u);
            CHECK(resources.liveResourceCount() == 0u);
        }
        resources.shutdown();
        CHECK_FALSE(resources.hadLeaksAtShutdown());
    }

    bgfx::shutdown();
}

TEST_CASE("hbg vfx backend maps command sink bindings to the encoder") {
    REQUIRE(initNoopRenderer());

    {
        hbg::gfx::ResourceManager resources;
        resources.init();
        {
            hbg::vfxbridge::HbgVfxBackend backend(resources);
            hbg::gfx::Encoder encoder(resources);
            backend.begin(encoder, 0);

            const vfx::BufferHandle attributes =
                backend.createBuffer({vfx::BufferUsage::StorageVertex, 256, 16, nullptr, "attributes"});
            const vfx::BufferHandle indirect =
                backend.createBuffer({vfx::BufferUsage::Indirect, 32, 32, nullptr, "indirect"});
            const std::vector<std::byte> shaderData = loadShader("minimal_compute_dxbc.bin");
            REQUIRE_FALSE(shaderData.empty());
            const std::array<vfx::ShaderBlob, 1> blobs{{
                {vfx::ShaderStage::Compute, backend.backend(), shaderData},
            }};
            const std::vector<std::byte> vertexData = loadShader("minimal_vertex_dxbc.bin");
            const std::vector<std::byte> fragmentData = loadShader("minimal_fragment_dxbc.bin");
            REQUIRE_FALSE(vertexData.empty());
            REQUIRE_FALSE(fragmentData.empty());
            const std::array<vfx::ShaderBlob, 2> graphicsBlobs{{
                {vfx::ShaderStage::Vertex, backend.backend(), vertexData},
                {vfx::ShaderStage::Fragment, backend.backend(), fragmentData},
            }};
            const vfx::ProgramHandle program = backend.createProgram(blobs);
            const vfx::UniformHandle params = backend.findUniform("u_vfxParams", vfx::UniformType::Vec4);
            const vfx::SamplerHandle sampler = backend.findSampler("s_vfxMain");
            CHECK(backend.findUniform("u_vfxParams", vfx::UniformType::Vec4) == params);
            CHECK(backend.findSampler("s_vfxMain") == sampler);
            const vfx::TextureHandle texture = backend.createTexture(
                {1, 1, 1, vfx::TextureUsage::Sampled, vfx::TextureFormat::RGBA8, nullptr, 0, "white"});

            const vfx::ProgramHandle graphicsProgram = backend.createProgram(graphicsBlobs);

            REQUIRE(attributes.valid());
            REQUIRE(indirect.valid());
            REQUIRE(program.valid());
            REQUIRE(graphicsProgram.valid());
            REQUIRE(params.valid());
            REQUIRE(sampler.valid());
            REQUIRE(texture.valid());

            const vfx::Vec4 value{1.0f, 0.0f, 0.0f, 1.0f};
            vfx::DrawState drawState;
            drawState.depthWrite = false;
            drawState.blend = vfx::BlendMode::Alpha;

            backend.beginPass({"VFX Simulation", true, false, false});
            backend.setStorageBuffer(0, attributes, vfx::Access::ReadWrite);
            backend.setTexture(1, sampler, texture);
            backend.setUniform(params, &value, 1);
            backend.setState(drawState);
            backend.dispatch(program, 1, 1, 1);
            backend.drawIndirect(program, indirect, 0, 1);
            backend.endPass();
            backend.end();

            CHECK(backend.liveBufferCount() == 3);
            CHECK(backend.liveProgramCount() == 2);
            CHECK(backend.liveTextureCount() == 1);
            CHECK(backend.liveUniformCount() == 1);
            CHECK(backend.liveSamplerCount() == 1);
            CHECK(backend.limits().maxTextureSamplers >= 16);
            CHECK(backend.limits().maxComputeBindings >= 16);
            CHECK(backend.lightingBindCount() == 0);

            backend.destroyBuffer(attributes);
            backend.destroyBuffer(indirect);
            backend.destroyProgram(program);
            backend.destroyProgram(graphicsProgram);
            backend.destroyTexture(texture);
            CHECK(backend.liveBufferCount() == 1);
            CHECK(backend.liveProgramCount() == 0);
            CHECK(backend.liveTextureCount() == 0);
        }
        CHECK(resources.liveResourceCount() == 0);
        resources.shutdown();
        CHECK_FALSE(resources.hadLeaksAtShutdown());
    }

    bgfx::shutdown();
}

TEST_CASE("hbg vfx backend releases resources across repeated runtime sessions") {
    REQUIRE(initNoopRenderer());

    {
        hbg::gfx::ResourceManager resources;
        resources.init();
        for (std::uint32_t session = 0; session < 4; ++session) {
            {
                hbg::vfxbridge::HbgVfxBackend backend(resources);
                vfx::Runtime runtime(backend.device(), &backend.shading());
                std::string error;
                const std::shared_ptr<const vfx::Asset> asset =
                    runtime.loadAsset(std::filesystem::path(VFX_SOURCE_DIR) / "tests/assets/minimal_unlit.vfxb",
                                      &error);
                INFO(error);
                REQUIRE(asset != nullptr);
                vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
                INFO(error);
                REQUIRE(effect != nullptr);

                hbg::gfx::Encoder encoder(resources);
                backend.begin(encoder, 0);
                runtime.update(0.1f);
                runtime.encodeSimulation(backend, {});
                backend.end();
                CHECK(runtime.diagnostics().dispatches == 6u);

                runtime.destroyEffect(effect);
                CHECK(backend.liveBufferCount() == 0u);
                CHECK(backend.liveProgramCount() == 0u);
                CHECK(backend.liveTextureCount() == 0u);
            }
            CHECK(resources.liveResourceCount() == 0u);
        }
        resources.shutdown();
        CHECK_FALSE(resources.hadLeaksAtShutdown());
    }

    bgfx::shutdown();
}

TEST_CASE("hbg vfx backend runs the runtime resource path from a baked asset") {
    REQUIRE(initNoopRenderer());

    {
        hbg::gfx::ResourceManager resources;
        resources.init();
        {
            hbg::vfxbridge::HbgVfxBackend backend(resources);
            vfx::Runtime runtime(backend.device(), &backend.shading());
            std::string error;
            const std::shared_ptr<const vfx::Asset> asset =
                runtime.loadAsset(std::filesystem::path(VFX_SOURCE_DIR) / "tests/assets/minimal_unlit.vfxb",
                                  &error);
            INFO(error);
            REQUIRE(asset != nullptr);
            vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
            INFO(error);
            REQUIRE(effect != nullptr);
            CHECK(backend.liveBufferCount() == 9u);
            CHECK(backend.liveProgramCount() == 7u);
            CHECK(backend.liveTextureCount() == 1u);
            CHECK(effect->isAlive());

            hbg::gfx::Encoder encoder(resources);
            backend.begin(encoder, 0);
            runtime.update(0.0f);
            runtime.encodeSimulation(backend, {});
            backend.end();
            CHECK(runtime.diagnostics().dispatches == 6u);

            runtime.destroyEffect(effect);
            CHECK(backend.liveBufferCount() == 0u);
            CHECK(backend.liveProgramCount() == 0u);
            CHECK(backend.liveTextureCount() == 0u);
        }
        CHECK(resources.liveResourceCount() == 0u);
        resources.shutdown();
        CHECK_FALSE(resources.hadLeaksAtShutdown());
    }

    bgfx::shutdown();
}

TEST_CASE("hbg vfx backend runs a lit effect and invokes lighting binding") {
    REQUIRE(initNoopRenderer());

    {
        hbg::gfx::ResourceManager resources;
        resources.init();
        {
            hbg::vfxbridge::HbgVfxBackend backend(resources);
            vfx::Runtime runtime(backend.device(), &backend.shading());
            std::string error;
            const std::shared_ptr<const vfx::Asset> asset =
                runtime.loadAsset(std::filesystem::path(VFX_SOURCE_DIR) / "tests/assets/lit_visual.vfxb",
                                  &error);
            INFO(error);
            REQUIRE(asset != nullptr);
            vfx::Effect* effect = runtime.createEffect(*asset, {}, &error);
            INFO(error);
            REQUIRE(effect != nullptr);
            CHECK(backend.liveBufferCount() == 9u);
            CHECK(backend.liveProgramCount() == 7u);
            CHECK(backend.liveTextureCount() == 1u);

            hbg::gfx::Encoder encoder(resources);
            backend.begin(encoder, 0);
            runtime.update(0.0f);
            vfx::FrameContext frame;
            runtime.encodeSimulation(backend, frame);
            runtime.encodeOutput(backend, frame);
            backend.end();
            CHECK(runtime.diagnostics().draws == 1u);
            CHECK(backend.lightingBindCount() == 1u);

            runtime.destroyEffect(effect);
            CHECK(backend.liveBufferCount() == 1u);
            CHECK(backend.liveProgramCount() == 0u);
            CHECK(backend.liveTextureCount() == 0u);
        }
        CHECK(resources.liveResourceCount() == 0u);
        resources.shutdown();
        CHECK_FALSE(resources.hadLeaksAtShutdown());
    }

    bgfx::shutdown();
}
