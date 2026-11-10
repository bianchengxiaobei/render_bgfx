#include "app/gfx_test.h"

#include "core/log.h"
#include "gfx/buffer.h"
#include "gfx/encoder.h"
#include "gfx/generated/uniforms.h"
#include "gfx/state.h"

#include <array>
#include <cstdint>
#include <string>

#ifndef HBG_BGFX_EXAMPLE_SHADERS
#define HBG_BGFX_EXAMPLE_SHADERS ""
#endif

namespace hbg {
namespace {

struct PosColorVertex {
    float position[3];
    uint32_t color;
};

constexpr PosColorVertex kCubeVertices[] = {
    {{-0.5f, -0.5f, -0.5f}, 0xffffffff}, {{0.5f, -0.5f, -0.5f}, 0xffffffff},
    {{0.5f, 0.5f, -0.5f}, 0xffffffff},   {{-0.5f, 0.5f, -0.5f}, 0xffffffff},
    {{-0.5f, -0.5f, 0.5f}, 0xffffffff},  {{0.5f, -0.5f, 0.5f}, 0xffffffff},
    {{0.5f, 0.5f, 0.5f}, 0xffffffff},    {{-0.5f, 0.5f, 0.5f}, 0xffffffff},
};

constexpr uint16_t kCubeIndices[] = {
    4, 5, 6, 4, 6, 7, 1, 0, 3, 1, 3, 2, 0, 4, 7, 0, 7, 3,
    5, 1, 2, 5, 2, 6, 3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4,
};

const char* shaderSubdirectory(bgfx::RendererType::Enum type) {
    switch (type) {
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return "dxbc";
    case bgfx::RendererType::Vulkan:
        return "spirv";
    case bgfx::RendererType::OpenGL:
        return "glsl";
    case bgfx::RendererType::Metal:
        return "metal";
    default:
        return nullptr;
    }
}

} // namespace

bool runGfxTest(gfx::ResourceManager& resources, uint16_t width, uint16_t height) {
    HBG_LOGI("running M0.2 gfx resource test");

    const std::array<uint8_t, 4 * 4 * 4> checker = {
        255, 255, 255, 255, 32, 32, 32, 255, 255, 255, 255, 255, 32, 32, 32, 255,
        32, 32, 32, 255,    255, 255, 255, 255, 32, 32, 32, 255,    255, 255, 255, 255,
        255, 255, 255, 255, 32, 32, 32, 255, 255, 255, 255, 255, 32, 32, 32, 255,
        32, 32, 32, 255,    255, 255, 255, 255, 32, 32, 32, 255,    255, 255, 255, 255,
    };

    gfx::TextureDesc sampledDesc;
    sampledDesc.width = 4;
    sampledDesc.height = 4;
    sampledDesc.format = gfx::TextureFormat::RGBA8;
    sampledDesc.flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_UVW_CLAMP;
    sampledDesc.data = checker.data();
    sampledDesc.dataSize = static_cast<uint32_t>(checker.size());
    sampledDesc.name = "checker";
    const gfx::TextureHandle sampled = resources.createTexture(sampledDesc);

    gfx::TextureDesc colorDesc;
    colorDesc.width = 256;
    colorDesc.height = 256;
    colorDesc.format = gfx::TextureFormat::RGBA8;
    colorDesc.renderTarget = true;
    colorDesc.name = "offscreenColor";
    const gfx::TextureHandle offscreenColor = resources.createTexture(colorDesc);

    gfx::TextureDesc depthDesc;
    depthDesc.width = 256;
    depthDesc.height = 256;
    depthDesc.format = gfx::TextureFormat::D24S8;
    depthDesc.depthStencil = true;
    depthDesc.name = "offscreenDepth";
    const gfx::TextureHandle offscreenDepth = resources.createTexture(depthDesc);

    const gfx::FrameBufferHandle offscreen =
        resources.createFrameBuffer({{offscreenColor, offscreenDepth}}, "offscreen");

    gfx::VertexLayout layout = gfx::VertexLayout::positionColor();
    gfx::BufferDesc vertexDesc;
    vertexDesc.type = gfx::BufferType::Vertex;
    vertexDesc.data = kCubeVertices;
    vertexDesc.size = sizeof(kCubeVertices);
    vertexDesc.layout = &layout;
    vertexDesc.name = "cubeVertices";
    const gfx::BufferHandle vertices = resources.createBuffer(vertexDesc);

    gfx::BufferDesc indexDesc;
    indexDesc.type = gfx::BufferType::Index;
    indexDesc.data = kCubeIndices;
    indexDesc.size = sizeof(kCubeIndices);
    indexDesc.name = "cubeIndices";
    const gfx::BufferHandle indices = resources.createBuffer(indexDesc);

    const gfx::UniformHandle colorSampler =
        resources.createUniform({"s_texColor", gfx::UniformType::Sampler, 1});

    const bool resourcesCreated = sampled.valid() && offscreenColor.valid() && offscreenDepth.valid() &&
                                  offscreen.valid() && vertices.valid() && indices.valid() &&
                                  colorSampler.valid();
    if (!resourcesCreated) {
        HBG_LOGE("gfx resource creation failed");
        return false;
    }

    gfx::generated::GlobalUniformBinder globalBinder;
    gfx::generated::PassUniformBinder passBinder;
    gfx::generated::MaterialUniformBinder materialBinder;
    if (!globalBinder.init(resources) || !passBinder.init(resources) || !materialBinder.init(resources)) {
        HBG_LOGE("generated uniform block initialization failed");
        return false;
    }

    gfx::generated::GlobalUniforms globalUniforms{};
    globalUniforms.view = hbg::Mat4::identity();
    globalUniforms.proj = hbg::Mat4::identity();
    globalUniforms.viewProj = hbg::Mat4::identity();
    globalUniforms.invViewProj = hbg::Mat4::identity();
    globalUniforms.prevViewProj = hbg::Mat4::identity();
    globalUniforms.screenSize = hbg::Vec4(static_cast<float>(width), static_cast<float>(height),
                                          1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
    globalUniforms.cameraPos = hbg::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    globalUniforms.time = hbg::Vec4(0.0f, 1.0f / 60.0f, 0.0f, 0.0f);
    for (hbg::Vec4& plane : globalUniforms.frustumPlanes) {
        plane = hbg::Vec4(0.0f, 0.0f, 1.0f, 0.0f);
    }

    gfx::generated::PassUniforms passUniforms{};
    passUniforms.shadowMatrix = hbg::Mat4::identity();
    passUniforms.shadowParams = hbg::Vec4(0.0f, 1.0f, 0.0f, 0.0f);

    gfx::generated::MaterialUniforms materialUniforms{};
    materialUniforms.baseColorFactor = hbg::Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    materialUniforms.metallicRoughness = hbg::Vec4(0.0f, 0.5f, 1.0f, 0.5f);
    materialUniforms.emissiveFactor = hbg::Vec4(0.0f, 0.0f, 0.0f, 0.0f);

    HBG_LOGI("generated uniform blocks initialized: global={} pass={} material={}",
             globalBinder.kDescriptors.size(), passBinder.kDescriptors.size(),
             materialBinder.kDescriptors.size());

    const char* shaderSubdir = shaderSubdirectory(bgfx::getRendererType());
    const std::string shaderRoot = HBG_BGFX_EXAMPLE_SHADERS;
    gfx::ProgramHandle program;
    if (shaderSubdir != nullptr && !shaderRoot.empty()) {
        const std::string directory = shaderRoot + "/" + shaderSubdir;
        program = resources.createProgramFromFiles(directory + "/vs_cubes.bin", directory + "/fs_cubes.bin", "cubes");
    }

    gfx::Encoder encoder(resources);
    const float identity[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    encoder.setViewRect(1, 0, 0, 256, 256);
    encoder.setViewFrameBuffer(1, offscreen);
    encoder.setViewClear(1, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x202020ff, 1.0f, 0);
    encoder.touch(1);

    const bool drewCube = program.valid();
    const auto drawCube = [&]() {
        if (!drewCube) {
            return;
        }
        encoder.setViewRect(0, 0, 0, width, height);
        encoder.setViewTransform(0, identity, identity);
        encoder.setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a1a1aff, 1.0f, 0);
        encoder.setState(gfx::state::kOpaqueNoCull);
        encoder.setTransform(identity);
        globalBinder.upload(globalUniforms);
        passBinder.upload(passUniforms);
        materialBinder.upload(materialUniforms);
        encoder.setVertexBuffer(0, vertices);
        encoder.setIndexBuffer(indices);
        encoder.submit(0, program);

        encoder.setViewRect(1, 0, 0, 256, 256);
        encoder.setViewFrameBuffer(1, offscreen);
        encoder.setViewTransform(1, identity, identity);
        encoder.setViewClear(1, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x202020ff, 1.0f, 0);
        encoder.setState(gfx::state::kOpaqueNoCull);
        encoder.setTransform(identity);
        globalBinder.upload(globalUniforms);
        passBinder.upload(passUniforms);
        materialBinder.upload(materialUniforms);
        encoder.setVertexBuffer(0, vertices);
        encoder.setIndexBuffer(indices);
        encoder.submit(1, program);
    };

    if (!drewCube) {
        HBG_LOGW("cubes shaders unavailable; skipping draw (shader root '{}')", shaderRoot);
    }

    drawCube();
    bgfx::frame();
    resources.endFrame();

    drawCube();
    bgfx::frame();
    resources.endFrame();

    drawCube();
    bgfx::frame();
    resources.endFrame();

    const bgfx::Stats* stats = bgfx::getStats();
    if (stats != nullptr) {
        HBG_LOGI("gfx test stats: draw={} compute={} textures={} triangles={} gpuFrame={}", stats->numDraw,
                 stats->numCompute, stats->numTextures, stats->numPrims[bgfx::Topology::TriList],
                 stats->gpuFrameNum);
    }

    bgfx::setViewRect(0, 0, 0, width, height);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a1a1aff, 1.0f, 0);
    bgfx::touch(0);
    bgfx::frame();
    resources.endFrame();

    resources.destroyFrameBuffer(offscreen);
    resources.destroyBuffer(indices);
    resources.destroyBuffer(vertices);
    if (program.valid()) {
        resources.destroyProgram(program);
    }
    globalBinder.shutdown(resources);
    passBinder.shutdown(resources);
    materialBinder.shutdown(resources);
    resources.destroyUniform(colorSampler);
    resources.destroyTexture(offscreenDepth);
    resources.destroyTexture(offscreenColor);
    resources.destroyTexture(sampled);
    resources.flushPending(true);

    const std::size_t live = resources.liveResourceCount();
    if (live != 0) {
        HBG_LOGE("gfx resource test leaked {} resources", live);
        return false;
    }

    HBG_LOGI("M0.2 gfx resource test passed (draw={})", drewCube);
    return true;
}

} // namespace hbg
