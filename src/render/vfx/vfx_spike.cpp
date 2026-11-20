#include "render/vfx/vfx_spike.h"

#include "core/log.h"
#include "core/math.h"
#include "gfx/buffer.h"
#include "gfx/encoder.h"
#include "gfx/generated/uniforms.h"
#include "gfx/state.h"
#include "gfx/texture.h"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifndef HBG_SHADER_ROOT
#define HBG_SHADER_ROOT ""
#endif

namespace hbg {
namespace {

constexpr uint32_t kParticleCount = 1024;
constexpr uint32_t kParticleStride = 16;
constexpr uint32_t kThreadGroupSize = 64;
constexpr uint16_t kColorWidth = 256;
constexpr uint16_t kColorHeight = 256;
constexpr uint8_t kUnlitView = 0;
constexpr uint8_t kLitView = 1;
constexpr uint8_t kAdditiveView = 2;
constexpr uint8_t kBlitView = 3;
constexpr uint8_t kComputeView = 4;
constexpr uint8_t kBackgroundR = 0x10;
constexpr uint8_t kBackgroundG = 0x10;
constexpr uint8_t kBackgroundB = 0x18;

uint64_t vfxBlendState(bool additive) {
    const uint64_t blend = additive ? BGFX_STATE_BLEND_ADD : BGFX_STATE_BLEND_ALPHA;
    return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL | blend | BGFX_STATE_MSAA;
}

const char* shaderSubdirectory() {
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return "dxbc";
    case bgfx::RendererType::Vulkan:
        return "spirv";
    default:
        return nullptr;
    }
}

std::string shaderPath(const char* file) {
    const char* subdirectory = shaderSubdirectory();
    if (subdirectory == nullptr || HBG_SHADER_ROOT[0] == '\0') {
        return {};
    }
    return std::string(HBG_SHADER_ROOT) + "/build/" + subdirectory + "/" + file;
}

struct VfxSpike {
    gfx::BufferHandle attributes;
    gfx::BufferHandle indirection;
    gfx::BufferHandle indirectArgs;
    gfx::BufferHandle shadowContext;
    gfx::BufferHandle dummyVertexBuffer;

    gfx::TextureHandle color;
    gfx::TextureHandle depth;
    gfx::TextureHandle readback;
    gfx::TextureHandle mainTexture;
    gfx::TextureHandle lightData;
    gfx::TextureHandle shadowMap;
    gfx::TextureHandle shadowMapPunctual;
    gfx::TextureHandle lightList;
    gfx::TextureHandle environment;
    gfx::TextureHandle contactShadow;
    gfx::TextureHandle probeMaps;
    gfx::TextureHandle cookieAtlas;

    gfx::FrameBufferHandle frameBuffer;

    gfx::ProgramHandle attributesProgram;
    gfx::ProgramHandle indirectProgram;
    gfx::ProgramHandle unlitProgram;
    gfx::ProgramHandle litProgram;

    gfx::UniformHandle paramsUniform;
    gfx::UniformHandle mainSampler;
    gfx::UniformHandle lightDataSampler;
    gfx::UniformHandle shadowMapSampler;
    gfx::UniformHandle punctualShadowSampler;
    gfx::UniformHandle lightListSampler;
    gfx::UniformHandle environmentSampler;
    gfx::UniformHandle contactShadowSampler;
    gfx::UniformHandle probeSampler;
    gfx::UniformHandle cookieSampler;

    gfx::generated::GlobalUniformBinder globalBinder;
    gfx::generated::PassUniformBinder passBinder;
    gfx::generated::ClusterUniformBinder clusterBinder;
    gfx::generated::EnvironmentUniformBinder environmentBinder;
    gfx::generated::ProbeUniformBinder probeBinder;
    gfx::generated::ContactUniformBinder contactBinder;
    gfx::generated::ObjectUniformBinder objectBinder;

    gfx::generated::GlobalUniforms globalUniforms{};
    gfx::generated::PassUniforms passUniforms{};
    gfx::generated::ClusterUniforms clusterUniforms{};
    gfx::generated::EnvironmentUniforms environmentUniforms{};
    gfx::generated::ProbeUniforms probeUniforms{};
    gfx::generated::ContactUniforms contactUniforms{};
    gfx::generated::ObjectUniforms objectUniforms{};
    Vec4 params{};
};

gfx::TextureHandle createTexture(gfx::ResourceManager& resources, const char* name, uint16_t width, uint16_t height,
                                 gfx::TextureFormat format, uint64_t flags, const void* data, uint32_t dataSize,
                                 uint16_t layers = 1) {
    gfx::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.layers = layers;
    desc.format = format;
    desc.flags = flags;
    desc.data = data;
    desc.dataSize = dataSize;
    desc.name = name;
    return resources.createTexture(desc);
}

bool createResources(VfxSpike& spike, gfx::ResourceManager& resources, uint16_t width, uint16_t height) {
    (void)width;
    (void)height;
    const std::array<uint8_t, 4> white = {255, 255, 255, 255};
    const std::array<uint32_t, 1> zeroUint = {0};

    gfx::VertexLayout layout;
    layout.add(bgfx::Attrib::Position, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
        .end();

    gfx::BufferDesc attributeDesc;
    attributeDesc.type = gfx::BufferType::StorageVertex;
    attributeDesc.count = kParticleCount;
    attributeDesc.layout = &layout;
    attributeDesc.flags = BGFX_BUFFER_COMPUTE_READ_WRITE;
    attributeDesc.name = "vfx.spike.attributes";
    spike.attributes = resources.createBuffer(attributeDesc);

    gfx::BufferDesc indirectionDesc;
    indirectionDesc.type = gfx::BufferType::Storage;
    indirectionDesc.count = kParticleCount;
    indirectionDesc.flags = BGFX_BUFFER_COMPUTE_READ_WRITE;
    indirectionDesc.name = "vfx.spike.indirection";
    spike.indirection = resources.createBuffer(indirectionDesc);

    gfx::BufferDesc indirectDesc;
    indirectDesc.type = gfx::BufferType::Indirect;
    indirectDesc.count = 1;
    indirectDesc.name = "vfx.spike.indirectArgs";
    spike.indirectArgs = resources.createBuffer(indirectDesc);

    const std::array<uint32_t, 64> shadowContext = {};
    gfx::BufferDesc shadowContextDesc;
    shadowContextDesc.type = gfx::BufferType::Storage;
    shadowContextDesc.data = shadowContext.data();
    shadowContextDesc.size = static_cast<uint32_t>(shadowContext.size() * sizeof(uint32_t));
    shadowContextDesc.count = static_cast<uint32_t>(shadowContext.size());
    shadowContextDesc.flags = BGFX_BUFFER_COMPUTE_READ;
    shadowContextDesc.name = "vfx.spike.shadowContext";
    spike.shadowContext = resources.createBuffer(shadowContextDesc);

    std::array<float, 6 * 3> dummyVertices = {};
    gfx::VertexLayout dummyLayout;
    dummyLayout.position().end();
    gfx::BufferDesc dummyDesc;
    dummyDesc.type = gfx::BufferType::Vertex;
    dummyDesc.data = dummyVertices.data();
    dummyDesc.size = static_cast<uint32_t>(dummyVertices.size() * sizeof(float));
    dummyDesc.layout = &dummyLayout;
    dummyDesc.name = "vfx.spike.dummyVertexBuffer";
    spike.dummyVertexBuffer = resources.createBuffer(dummyDesc);

    constexpr uint64_t kSamplerFlags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    spike.mainTexture =
        createTexture(resources, "vfx.spike.main", 1, 1, gfx::TextureFormat::RGBA8, kSamplerFlags, white.data(),
                      static_cast<uint32_t>(white.size()));
    spike.environment =
        createTexture(resources, "vfx.spike.environment", 1, 1, gfx::TextureFormat::RGBA8, kSamplerFlags,
                      white.data(), static_cast<uint32_t>(white.size()));
    spike.contactShadow =
        createTexture(resources, "vfx.spike.contactShadow", 1, 1, gfx::TextureFormat::RGBA8, kSamplerFlags,
                      white.data(), static_cast<uint32_t>(white.size()));
    spike.cookieAtlas =
        createTexture(resources, "vfx.spike.cookieAtlas", 1, 1, gfx::TextureFormat::RGBA8, kSamplerFlags,
                      white.data(), static_cast<uint32_t>(white.size()));
    spike.probeMaps =
        createTexture(resources, "vfx.spike.probeMaps", 1, 1, gfx::TextureFormat::RGBA8, kSamplerFlags,
                      white.data(), static_cast<uint32_t>(white.size()), 1);
    spike.lightList =
        createTexture(resources, "vfx.spike.lightList", 1, 1, gfx::TextureFormat::R32U, kSamplerFlags,
                      zeroUint.data(), static_cast<uint32_t>(zeroUint.size() * sizeof(uint32_t)));
    spike.shadowMap =
        createTexture(resources, "vfx.spike.shadowMap", 1, 1, gfx::TextureFormat::D32F, kSamplerFlags, nullptr, 0);
    spike.shadowMapPunctual = createTexture(resources, "vfx.spike.shadowMapPunctual", 1, 1, gfx::TextureFormat::D32F,
                                            kSamplerFlags, nullptr, 0);

    std::array<float, 64 * 4> lightData = {};
    const auto setTexel = [&lightData](uint32_t index, const Vec4& value) {
        lightData[index * 4 + 0] = value.x;
        lightData[index * 4 + 1] = value.y;
        lightData[index * 4 + 2] = value.z;
        lightData[index * 4 + 3] = value.w;
    };
    setTexel(0, Vec4(0.0f, 0.0f, 0.0f, 0.0f));
    setTexel(1, Vec4(1.0f, 1.0f, 1.0f, 2.0f));
    setTexel(2, Vec4(0.0f, 0.0f, -1.0f, 0.0f));
    setTexel(3, Vec4(-1.0f, 0.0f, 0.0f, 0.0f));
    setTexel(4, Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    setTexel(6, Vec4(1.0f, 0.0f, 0.0f, 0.0f));
    spike.lightData = createTexture(resources, "vfx.spike.lightData", 64, 1, gfx::TextureFormat::RGBA32F,
                                    kSamplerFlags, lightData.data(),
                                    static_cast<uint32_t>(lightData.size() * sizeof(float)));

    gfx::TextureDesc colorDesc;
    colorDesc.width = kColorWidth;
    colorDesc.height = kColorHeight;
    colorDesc.format = gfx::TextureFormat::RGBA8;
    colorDesc.renderTarget = true;
    colorDesc.name = "vfx.spike.color";
    spike.color = resources.createTexture(colorDesc);

    gfx::TextureDesc depthDesc;
    depthDesc.width = kColorWidth;
    depthDesc.height = kColorHeight;
    depthDesc.format = gfx::TextureFormat::D24S8;
    depthDesc.depthStencil = true;
    depthDesc.name = "vfx.spike.depth";
    spike.depth = resources.createTexture(depthDesc);

    gfx::TextureDesc readbackDesc;
    readbackDesc.width = kColorWidth;
    readbackDesc.height = kColorHeight;
    readbackDesc.format = gfx::TextureFormat::RGBA8;
    readbackDesc.flags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK;
    readbackDesc.name = "vfx.spike.readback";
    spike.readback = resources.createTexture(readbackDesc);

    gfx::FrameBufferKey frameBufferKey;
    frameBufferKey.attachments = {spike.color, spike.depth};
    spike.frameBuffer = resources.createFrameBuffer(frameBufferKey, "vfx.spike.frameBuffer");

    spike.attributesProgram =
        resources.createComputeProgramFromFiles(shaderPath("vfx_spike_attributes.cs.bin"), "vfx.spike.attributes");
    spike.indirectProgram =
        resources.createComputeProgramFromFiles(shaderPath("vfx_spike_indirect.cs.bin"), "vfx.spike.indirect");
    spike.unlitProgram = resources.createProgramFromFiles(shaderPath("vfx_spike.vs.bin"),
                                                          shaderPath("vfx_spike_unlit.fs.bin"), "vfx.spike.unlit");
    spike.litProgram =
        resources.createProgramFromFiles(shaderPath("vfx_spike.vs.bin"), shaderPath("vfx_spike_lit.fs.bin"),
                                         "vfx.spike.lit");

    spike.paramsUniform = resources.createUniform({"u_vfx_params", gfx::UniformType::Vec4, 1});
    spike.mainSampler = resources.createUniform({"s_vfxMain", gfx::UniformType::Sampler, 1});
    spike.lightDataSampler = resources.createUniform({"s_lightData", gfx::UniformType::Sampler, 1});
    spike.shadowMapSampler = resources.createUniform({"s_shadowMap", gfx::UniformType::Sampler, 1});
    spike.punctualShadowSampler = resources.createUniform({"s_shadowMapPunctual", gfx::UniformType::Sampler, 1});
    spike.lightListSampler = resources.createUniform({"s_lightList", gfx::UniformType::Sampler, 1});
    spike.environmentSampler = resources.createUniform({"s_environmentMap", gfx::UniformType::Sampler, 1});
    spike.contactShadowSampler = resources.createUniform({"s_contactShadow", gfx::UniformType::Sampler, 1});
    spike.probeSampler = resources.createUniform({"s_probeMaps", gfx::UniformType::Sampler, 1});
    spike.cookieSampler = resources.createUniform({"s_cookieAtlas", gfx::UniformType::Sampler, 1});

    const bool resourcesValid =
        spike.attributes.valid() && spike.indirection.valid() && spike.indirectArgs.valid() &&
        spike.shadowContext.valid() && spike.dummyVertexBuffer.valid() && spike.color.valid() && spike.depth.valid() && spike.readback.valid() &&
        spike.mainTexture.valid() && spike.lightData.valid() && spike.shadowMap.valid() &&
        spike.shadowMapPunctual.valid() && spike.lightList.valid() && spike.environment.valid() &&
        spike.contactShadow.valid() && spike.probeMaps.valid() && spike.cookieAtlas.valid() &&
        spike.frameBuffer.valid() && spike.attributesProgram.valid() && spike.indirectProgram.valid() &&
        spike.unlitProgram.valid() && spike.litProgram.valid() && spike.paramsUniform.valid() &&
        spike.mainSampler.valid() && spike.lightDataSampler.valid() && spike.shadowMapSampler.valid() &&
        spike.punctualShadowSampler.valid() && spike.lightListSampler.valid() && spike.environmentSampler.valid() &&
        spike.contactShadowSampler.valid() && spike.probeSampler.valid() && spike.cookieSampler.valid();
    if (!resourcesValid) {
        HBG_LOGE("vfx spike: resource creation failed");
        return false;
    }

    if (!spike.globalBinder.init(resources) || !spike.passBinder.init(resources) ||
        !spike.clusterBinder.init(resources) || !spike.environmentBinder.init(resources) ||
        !spike.probeBinder.init(resources) || !spike.contactBinder.init(resources) ||
        !spike.objectBinder.init(resources)) {
        HBG_LOGE("vfx spike: uniform binder initialization failed");
        return false;
    }

    const Mat4 identity = Mat4::identity();
    const Mat4 view = lookAtRH(Vec3(0.0f, 0.0f, -3.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 projection = perspectiveRH(1.0471975512f, static_cast<float>(width) / static_cast<float>(height), 0.1f,
                                          100.0f, bgfx::getCaps()->homogeneousDepth);
    const Mat4 viewProjection = projection * view;

    spike.globalUniforms.view = view;
    spike.globalUniforms.invView = inverse(view);
    spike.globalUniforms.proj = projection;
    spike.globalUniforms.invProj = inverse(projection);
    spike.globalUniforms.viewProj = viewProjection;
    spike.globalUniforms.invViewProj = inverse(viewProjection);
    spike.globalUniforms.nonJitteredViewProj = viewProjection;
    spike.globalUniforms.nonJitteredInvViewProj = inverse(viewProjection);
    spike.globalUniforms.prevViewProj = viewProjection;
    spike.globalUniforms.prevInvViewProj = inverse(viewProjection);
    spike.globalUniforms.screenSize =
        Vec4(static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width),
             1.0f / static_cast<float>(height));
    spike.globalUniforms.cameraPos = Vec4(0.0f, 0.0f, -3.0f, 1.0f);
    spike.globalUniforms.prevCameraPos = spike.globalUniforms.cameraPos;
    spike.globalUniforms.taaJitter = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    spike.globalUniforms.taaFrameInfo = Vec4(0.0f, 1.0f / 60.0f, 0.0f, 1.0f);
    spike.globalUniforms.rtScale = Vec4(1.0f, 1.0f, 0.0f, 0.0f);
    spike.globalUniforms.dynamicResolutionScale = Vec4(1.0f, 1.0f, 0.0f, 0.0f);
    spike.globalUniforms.time = Vec4(0.0f, 1.0f / 60.0f, 0.0f, 0.0f);
    for (Vec4& plane : spike.globalUniforms.frustumPlanes) {
        plane = Vec4(0.0f, 0.0f, 1.0f, 0.0f);
    }

    spike.passUniforms.shadowMatrix = identity;
    spike.passUniforms.shadowViewProj = identity;
    spike.passUniforms.shadowParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    spike.passUniforms.shadowCascadeSplits = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    spike.passUniforms.lightDirection = Vec4(0.0f, 0.0f, -1.0f, 1.0f);
    spike.passUniforms.lightColor = Vec4(1.0f, 1.0f, 1.0f, 0.0f);
    spike.passUniforms.ambientColor = Vec4(0.15f, 0.18f, 0.25f, 1.0f);
    spike.passUniforms.lightCounts = Vec4(1.0f, 1.0f, 0.0f, 0.0f);
    spike.passUniforms.lightListParams = Vec4(16.0f, 1.0f, 1.0f, 0.0f);
    spike.passUniforms.lightListParams2 = Vec4(16.0f, 32.0f, 0.0f, 0.0f);
    spike.passUniforms.lightScreenSize = spike.globalUniforms.screenSize;
    spike.passUniforms.cookieAtlasParams = Vec4(1.0f, 0.0f, 0.0f, 0.0f);
    spike.passUniforms.litVariantParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    spike.passUniforms.lightmapParams2 = Vec4(0.0f, 0.0f, 0.0f, 0.0f);

    spike.clusterUniforms.screenSize = spike.globalUniforms.screenSize;
    spike.clusterUniforms.depthParams = Vec4(0.0f, 0.0f, 1.0f, 0.0f);

    spike.environmentUniforms.skyParams = Vec4(1.0f, 0.0f, 0.0f, 0.0f);

    spike.objectUniforms.prevModel = identity;
    spike.objectUniforms.lightLayerMask = Vec4(1.0f, 0.0f, 0.0f, 0.0f);
    spike.objectUniforms.lightmapST = Vec4(1.0f, 1.0f, 0.0f, 0.0f);

    spike.params = Vec4(static_cast<float>(kParticleCount), 0.0f, 0.0f, 0.0f);
    return true;
}

void destroyResources(VfxSpike& spike, gfx::ResourceManager& resources) {
    spike.globalBinder.shutdown(resources);
    spike.passBinder.shutdown(resources);
    spike.clusterBinder.shutdown(resources);
    spike.environmentBinder.shutdown(resources);
    spike.probeBinder.shutdown(resources);
    spike.contactBinder.shutdown(resources);
    spike.objectBinder.shutdown(resources);

    if (spike.paramsUniform.valid()) {
        resources.destroyUniform(spike.paramsUniform);
    }
    if (spike.mainSampler.valid()) {
        resources.destroyUniform(spike.mainSampler);
    }
    if (spike.lightDataSampler.valid()) {
        resources.destroyUniform(spike.lightDataSampler);
    }
    if (spike.shadowMapSampler.valid()) {
        resources.destroyUniform(spike.shadowMapSampler);
    }
    if (spike.punctualShadowSampler.valid()) {
        resources.destroyUniform(spike.punctualShadowSampler);
    }
    if (spike.lightListSampler.valid()) {
        resources.destroyUniform(spike.lightListSampler);
    }
    if (spike.environmentSampler.valid()) {
        resources.destroyUniform(spike.environmentSampler);
    }
    if (spike.contactShadowSampler.valid()) {
        resources.destroyUniform(spike.contactShadowSampler);
    }
    if (spike.probeSampler.valid()) {
        resources.destroyUniform(spike.probeSampler);
    }
    if (spike.cookieSampler.valid()) {
        resources.destroyUniform(spike.cookieSampler);
    }

    if (spike.frameBuffer.valid()) {
        resources.destroyFrameBuffer(spike.frameBuffer);
    }
    if (spike.unlitProgram.valid()) {
        resources.destroyProgram(spike.unlitProgram);
    }
    if (spike.litProgram.valid()) {
        resources.destroyProgram(spike.litProgram);
    }
    if (spike.attributesProgram.valid()) {
        resources.destroyProgram(spike.attributesProgram);
    }
    if (spike.indirectProgram.valid()) {
        resources.destroyProgram(spike.indirectProgram);
    }
    if (spike.attributes.valid()) {
        resources.destroyBuffer(spike.attributes);
    }
    if (spike.indirection.valid()) {
        resources.destroyBuffer(spike.indirection);
    }
    if (spike.indirectArgs.valid()) {
        resources.destroyBuffer(spike.indirectArgs);
    }
    if (spike.shadowContext.valid()) {
        resources.destroyBuffer(spike.shadowContext);
    }
    if (spike.dummyVertexBuffer.valid()) {
        resources.destroyBuffer(spike.dummyVertexBuffer);
    }
    if (spike.color.valid()) {
        resources.destroyTexture(spike.color);
    }
    if (spike.depth.valid()) {
        resources.destroyTexture(spike.depth);
    }
    if (spike.readback.valid()) {
        resources.destroyTexture(spike.readback);
    }
    if (spike.mainTexture.valid()) {
        resources.destroyTexture(spike.mainTexture);
    }
    if (spike.lightData.valid()) {
        resources.destroyTexture(spike.lightData);
    }
    if (spike.shadowMap.valid()) {
        resources.destroyTexture(spike.shadowMap);
    }
    if (spike.shadowMapPunctual.valid()) {
        resources.destroyTexture(spike.shadowMapPunctual);
    }
    if (spike.lightList.valid()) {
        resources.destroyTexture(spike.lightList);
    }
    if (spike.environment.valid()) {
        resources.destroyTexture(spike.environment);
    }
    if (spike.contactShadow.valid()) {
        resources.destroyTexture(spike.contactShadow);
    }
    if (spike.probeMaps.valid()) {
        resources.destroyTexture(spike.probeMaps);
    }
    if (spike.cookieAtlas.valid()) {
        resources.destroyTexture(spike.cookieAtlas);
    }
    resources.flushPending(true);
}

bool readBuffer(gfx::ResourceManager& resources, gfx::BufferHandle buffer, void* data, uint32_t size,
                const char* label) {
    const uint32_t expectedFrame =
        bgfx::read({.handle = resources.native(buffer), .offset = 0, .size = size}, data);
    if (expectedFrame == 0) {
        HBG_LOGE("vfx spike: {} readback request failed", label);
        return false;
    }
    uint32_t currentFrame = bgfx::frame();
    resources.endFrame();
    while (currentFrame < expectedFrame) {
        currentFrame = bgfx::frame();
        resources.endFrame();
    }
    return true;
}

bool readColor(gfx::ResourceManager& resources, const VfxSpike& spike, std::vector<uint8_t>& pixels) {
    bgfx::setViewRect(kBlitView, 0, 0, kColorWidth, kColorHeight);
    bgfx::blit(kBlitView,
               {.handle = resources.native(spike.readback), .width = kColorWidth, .height = kColorHeight},
               {.handle = resources.native(spike.color), .width = kColorWidth, .height = kColorHeight});
    bgfx::touch(kBlitView);

    pixels.resize(static_cast<std::size_t>(kColorWidth) * kColorHeight * 4);
    const uint32_t expectedFrame =
        bgfx::read({.handle = resources.native(spike.readback), .width = kColorWidth, .height = kColorHeight},
                   pixels.data());
    if (expectedFrame == 0) {
        HBG_LOGE("vfx spike: color readback request failed");
        return false;
    }
    uint32_t currentFrame = bgfx::frame();
    resources.endFrame();
    while (currentFrame < expectedFrame) {
        currentFrame = bgfx::frame();
        resources.endFrame();
    }
    return true;
}

uint32_t countParticles(const std::vector<uint8_t>& pixels) {
    uint32_t count = 0;
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
        const int dr = std::abs(static_cast<int>(pixels[index]) - kBackgroundR);
        const int dg = std::abs(static_cast<int>(pixels[index + 1]) - kBackgroundG);
        const int db = std::abs(static_cast<int>(pixels[index + 2]) - kBackgroundB);
        if (dr > 2 || dg > 2 || db > 2) {
            ++count;
        }
    }
    return count;
}

double averageLuminance(const std::vector<uint8_t>& pixels) {
    double sum = 0.0;
    uint32_t count = 0;
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
        const int dr = std::abs(static_cast<int>(pixels[index]) - kBackgroundR);
        const int dg = std::abs(static_cast<int>(pixels[index + 1]) - kBackgroundG);
        const int db = std::abs(static_cast<int>(pixels[index + 2]) - kBackgroundB);
        if (dr > 2 || dg > 2 || db > 2) {
            sum += (static_cast<double>(pixels[index]) + pixels[index + 1] + pixels[index + 2]) / (3.0 * 255.0);
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

void bindOutputTextures(gfx::Encoder& encoder, const VfxSpike& spike, bool lit) {
    if (!lit) {
        encoder.setTexture(0, spike.mainSampler, spike.mainTexture);
        return;
    }
    encoder.setTexture(5, spike.lightDataSampler, spike.lightData);
    encoder.setTexture(6, spike.shadowMapSampler, spike.shadowMap);
    encoder.setTexture(7, spike.lightListSampler, spike.lightList);
    encoder.setTexture(9, spike.punctualShadowSampler, spike.shadowMapPunctual);
    encoder.setTexture(10, spike.environmentSampler, spike.environment);
    encoder.setTexture(11, spike.contactShadowSampler, spike.contactShadow);
    encoder.setTexture(14, spike.probeSampler, spike.probeMaps);
    encoder.setTexture(15, spike.cookieSampler, spike.cookieAtlas);
}

void uploadFrameUniforms(gfx::Encoder& encoder, const VfxSpike& spike, bool lit) {
    spike.globalBinder.upload(spike.globalUniforms);
    encoder.setUniform(spike.paramsUniform, &spike.params, 1);
    if (lit) {
        spike.passBinder.upload(spike.passUniforms);
        spike.clusterBinder.upload(spike.clusterUniforms);
        spike.environmentBinder.upload(spike.environmentUniforms);
        spike.probeBinder.upload(spike.probeUniforms);
        spike.contactBinder.upload(spike.contactUniforms);
        spike.objectBinder.upload(spike.objectUniforms);
    }
    (void)encoder;
}

bool run(gfx::ResourceManager& resources, const VfxSpike& spike) {
    gfx::Encoder encoder(resources);

    encoder.setViewRect(kComputeView, 0, 0, kColorWidth, kColorHeight);
    encoder.setUniform(spike.paramsUniform, &spike.params, 1);
    encoder.setStorageBuffer(0, spike.attributes, bgfx::Access::Write);
    encoder.setStorageBuffer(1, spike.indirection, bgfx::Access::Write);
    encoder.dispatch(kComputeView, spike.attributesProgram, kParticleCount / kThreadGroupSize, 1, 1);
    encoder.setUniform(spike.paramsUniform, &spike.params, 1);
    encoder.setStorageBuffer(0, spike.indirectArgs, bgfx::Access::Write);
    encoder.dispatch(kComputeView, spike.indirectProgram, 1, 1, 1);
    bgfx::frame();
    resources.endFrame();

    std::vector<uint32_t> attributeData(static_cast<std::size_t>(kParticleCount) * kParticleStride);
    if (!readBuffer(resources, spike.attributes, attributeData.data(),
                    static_cast<uint32_t>(attributeData.size() * sizeof(uint32_t)), "attributes")) {
        return false;
    }
    std::vector<uint32_t> indirectionData(kParticleCount);
    if (!readBuffer(resources, spike.indirection, indirectionData.data(),
                    static_cast<uint32_t>(indirectionData.size() * sizeof(uint32_t)), "indirection")) {
        return false;
    }
    std::array<uint32_t, 8> indirectData = {};
    if (!readBuffer(resources, spike.indirectArgs, indirectData.data(), static_cast<uint32_t>(indirectData.size() * sizeof(uint32_t)),
                    "indirect args")) {
        return false;
    }

    const float positionX = std::bit_cast<float>(attributeData[0]);
    const float positionY = std::bit_cast<float>(attributeData[1]);
    const float size = std::bit_cast<float>(attributeData[3]);
    const float colorR = std::bit_cast<float>(attributeData[4]);
    const float colorA = std::bit_cast<float>(attributeData[7]);
    const bool attributeOk = std::fabs(positionX + 1.24f) < 1.0e-5f && std::fabs(positionY + 1.24f) < 1.0e-5f &&
                             std::fabs(size - 0.06f) < 1.0e-6f && std::fabs(colorR - 0.08f) < 1.0e-6f &&
                             std::fabs(colorA - 1.0f) < 1.0e-6f;
    const bool indirectionOk = indirectionData[0] == 0u && indirectionData[kParticleCount - 1] == kParticleCount - 1u;
    const bool indirectOk = indirectData[0] == kParticleCount * 6u && indirectData[1] == 1u &&
                            indirectData[2] == 0u && indirectData[3] == 0u && indirectData[4] == 0u &&
                            indirectData[5] == 0u && indirectData[6] == 0u && indirectData[7] == 0u;
    HBG_LOGI("vfx spike readback: position=({:.4f},{:.4f}) size={:.4f} colorR={:.4f} alpha={:.4f} "
             "args=[{} {} {} {} {} {} {} {}] attrs={} indirection={} indirect={}",
             positionX, positionY, size, colorR, colorA, indirectData[0], indirectData[1], indirectData[2],
             indirectData[3], indirectData[4], indirectData[5], indirectData[6], indirectData[7],
             attributeOk ? "ok" : "FAIL", indirectionOk ? "ok" : "FAIL", indirectOk ? "ok" : "FAIL");
    if (!attributeOk || !indirectionOk || !indirectOk) {
        return false;
    }

    const Vec4 emptyParams(0.0f, 0.0f, 0.0f, 0.0f);
    const auto encodeStart = std::chrono::steady_clock::now();
    for (uint32_t index = 0; index < 100; ++index) {
        encoder.setUniform(spike.paramsUniform, &emptyParams, 1);
        encoder.setStorageBuffer(0, spike.attributes, bgfx::Access::Write);
        encoder.dispatch(kComputeView, spike.attributesProgram, 1, 1, 1);
    }
    const auto encodeEnd = std::chrono::steady_clock::now();
    bgfx::frame();
    resources.endFrame();
    const auto frameEnd = std::chrono::steady_clock::now();
    const double encodeMs =
        std::chrono::duration<double, std::milli>(encodeEnd - encodeStart).count();
    const double frameMs =
        std::chrono::duration<double, std::milli>(frameEnd - encodeEnd).count();
    HBG_LOGI("vfx spike empty dispatch x100: encode_ms={:.3f} frame_ms={:.3f}", encodeMs, frameMs);

    const auto renderOutput = [&](uint8_t view, gfx::ProgramHandle program, bool lit, bool additive,
                                  const char* label) {
        encoder.setViewRect(view, 0, 0, kColorWidth, kColorHeight);
        encoder.setViewFrameBuffer(view, spike.frameBuffer);
        encoder.setViewClear(view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x101018ff, 1.0f, 0);
        encoder.setViewTransform(view, spike.globalUniforms.view.values.data(),
                                 spike.globalUniforms.proj.values.data());
        uploadFrameUniforms(encoder, spike, lit);
        bindOutputTextures(encoder, spike, lit);
        encoder.setVertexBuffer(0, spike.dummyVertexBuffer);
        encoder.setStorageBuffer(12, spike.attributes, bgfx::Access::Read);
        encoder.setStorageBuffer(13, spike.indirection, bgfx::Access::Read);
        if (lit) {
            encoder.setStorageBuffer(8, spike.shadowContext, bgfx::Access::Read);
        }
        encoder.setState(vfxBlendState(additive));
        encoder.submitIndirect(view, program, spike.indirectArgs, 0, 1);
        bgfx::frame();
        resources.endFrame();

        std::vector<uint8_t> pixels;
        if (!readColor(resources, spike, pixels)) {
            return false;
        }
        const uint32_t count = countParticles(pixels);
        HBG_LOGI("vfx spike {}: pixels={} luminance={:.4f}", label, count, averageLuminance(pixels));
        return count > 0;
    };

    if (!renderOutput(kUnlitView, spike.unlitProgram, false, false, "unlit alpha")) {
        return false;
    }
    if (!renderOutput(kLitView, spike.litProgram, true, false, "lit")) {
        return false;
    }
    if (!renderOutput(kAdditiveView, spike.unlitProgram, false, true, "unlit additive")) {
        return false;
    }
    return true;
}

} // namespace

bool runVfxSpike(gfx::ResourceManager& resources, uint16_t width, uint16_t height) {
    HBG_LOGI("running VFX V0 spike ({}x{}, backend={})", width, height, bgfx::getRendererName(bgfx::getRendererType()));
    if (shaderPath("vfx_spike.vs.bin").empty()) {
        HBG_LOGE("vfx spike: shader root/backed is unavailable");
        return false;
    }

    VfxSpike spike;
    bool ok = createResources(spike, resources, width, height);
    if (ok) {
        ok = run(resources, spike);
    }
    destroyResources(spike, resources);
    const std::size_t live = resources.liveResourceCount();
    if (live != 0) {
        HBG_LOGE("vfx spike leaked {} resources", live);
        ok = false;
    }
    if (ok) {
        HBG_LOGI("VFX V0 spike passed");
    }
    return ok;
}

} // namespace hbg
