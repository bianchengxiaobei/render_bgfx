#include "app/sg_spike.h"

#include "app/sg_http_spike.h"
#include "app/sg_jpeg.h"
#include "core/log.h"
#include "core/math.h"
#include "gfx/buffer.h"
#include "gfx/encoder.h"
#include "gfx/generated/uniforms.h"
#include "gfx/texture.h"
#include "sgb.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef HBG_SG_SPIKE_ASSET_DEFAULT
#define HBG_SG_SPIKE_ASSET_DEFAULT "tests/shadergraph/assets/spike.sgb"
#endif

namespace hbg {
namespace {

constexpr std::uint16_t kSpikeSize = 512;
constexpr bgfx::ViewId kSpikeView = 8;
constexpr bgfx::ViewId kSpikeBlitView = 9;
constexpr std::uint32_t kGraphStageBudget = 5;
constexpr std::uint32_t kRingCount = 24;
constexpr std::uint32_t kSegmentCount = 48;

struct SpikeVertex {
    float position[3];
    float normal[3];
    float tangent[4];
    float uv[2];
};

struct SgSpike {
    gfx::ProgramHandle program;
    gfx::BufferHandle vertexBuffer;
    gfx::BufferHandle indexBuffer;
    gfx::BufferHandle shadowContext;

    gfx::TextureHandle color;
    gfx::TextureHandle depth;
    gfx::TextureHandle readback;
    gfx::TextureHandle mainTexture;
    std::array<gfx::TextureHandle, 4> dummyTextures{};
    gfx::TextureHandle lightData;
    gfx::TextureHandle shadowMap;
    gfx::TextureHandle punctualShadow;
    gfx::TextureHandle lightList;
    gfx::TextureHandle environment;
    gfx::TextureHandle contactShadow;
    gfx::TextureHandle probeMaps;
    gfx::TextureHandle cookieAtlas;
    gfx::FrameBufferHandle frameBuffer;

    gfx::UniformHandle mainSampler;
    std::array<gfx::UniformHandle, 4> dummySamplers{};
    gfx::UniformHandle lightDataSampler;
    gfx::UniformHandle shadowMapSampler;
    gfx::UniformHandle lightListSampler;
    gfx::UniformHandle punctualShadowSampler;
    gfx::UniformHandle environmentSampler;
    gfx::UniformHandle contactShadowSampler;
    gfx::UniformHandle probeSampler;
    gfx::UniformHandle cookieSampler;
    gfx::UniformHandle tintUniform;
    gfx::UniformHandle smoothnessUniform;
    gfx::UniformHandle debugTexturesUniform;

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

    Vec4 tint{0.95f, 0.85f, 0.75f, 1.0f};
    Vec4 smoothness{0.35f, 0.0f, 0.0f, 0.0f};
    Vec4 debugTextures{0.0f, 0.0f, 0.0f, 0.0f};
    Mat4 model = Mat4::identity();
    std::uint32_t indexCount = 0;
};

std::string makeToken() {
    std::random_device device;
    std::mt19937 generator(device());
    std::uniform_int_distribution<int> distribution(0, 15);
    constexpr char kHex[] = "0123456789abcdef";
    std::string token(16, '0');
    for (char& character : token) {
        character = kHex[distribution(generator)];
    }
    return token;
}

bool isBackendSupported(bgfx::RendererType::Enum type) {
    return type == bgfx::RendererType::Direct3D11 || type == bgfx::RendererType::Direct3D12 ||
           type == bgfx::RendererType::Vulkan;
}

sg::spike::ShaderBackend backendForRenderer(bgfx::RendererType::Enum type) {
    return type == bgfx::RendererType::Vulkan ? sg::spike::ShaderBackend::Spirv
                                              : sg::spike::ShaderBackend::Dxbc;
}

gfx::TextureHandle createTexture(gfx::ResourceManager& resources, const char* name, std::uint16_t width,
                                 std::uint16_t height, gfx::TextureFormat format, std::uint64_t flags,
                                 const void* data, std::uint32_t dataSize, std::uint16_t layers = 1) {
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

std::vector<std::uint8_t> makeCheckerTexture() {
    constexpr std::uint32_t size = 256;
    constexpr std::uint32_t square = 32;
    std::vector<std::uint8_t> pixels(size * size * 4);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const bool bright = ((x / square) + (y / square)) % 2 == 0;
            const std::uint8_t r = bright ? 220 : 24;
            const std::uint8_t g = bright ? 110 : 80;
            const std::uint8_t b = bright ? 48 : 110;
            const std::size_t index = (static_cast<std::size_t>(y) * size + x) * 4;
            pixels[index + 0] = r;
            pixels[index + 1] = g;
            pixels[index + 2] = b;
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

void buildSphere(std::vector<SpikeVertex>& vertices, std::vector<std::uint16_t>& indices) {
    constexpr float kPi = 3.14159265358979323846f;
    vertices.clear();
    vertices.reserve((kRingCount + 1) * (kSegmentCount + 1));
    for (std::uint32_t ring = 0; ring <= kRingCount; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(kRingCount);
        const float theta = kPi * v;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);
        for (std::uint32_t segment = 0; segment <= kSegmentCount; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(kSegmentCount);
            const float phi = 2.0f * kPi * u;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);
            SpikeVertex vertex{};
            vertex.position[0] = sinTheta * cosPhi;
            vertex.position[1] = cosTheta;
            vertex.position[2] = sinTheta * sinPhi;
            vertex.normal[0] = vertex.position[0];
            vertex.normal[1] = vertex.position[1];
            vertex.normal[2] = vertex.position[2];
            vertex.tangent[0] = -sinPhi;
            vertex.tangent[1] = 0.0f;
            vertex.tangent[2] = cosPhi;
            vertex.tangent[3] = 1.0f;
            vertex.uv[0] = u;
            vertex.uv[1] = v;
            vertices.push_back(vertex);
        }
    }

    indices.clear();
    indices.reserve(kRingCount * kSegmentCount * 6);
    for (std::uint32_t ring = 0; ring < kRingCount; ++ring) {
        for (std::uint32_t segment = 0; segment < kSegmentCount; ++segment) {
            const std::uint16_t a = static_cast<std::uint16_t>(ring * (kSegmentCount + 1) + segment);
            const std::uint16_t b = static_cast<std::uint16_t>(a + 1);
            const std::uint16_t c = static_cast<std::uint16_t>(a + kSegmentCount + 1);
            const std::uint16_t d = static_cast<std::uint16_t>(c + 1);
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }
}

bool createResources(SgSpike& spike, gfx::ResourceManager& resources, const sg::spike::Asset& asset,
                     sg::spike::ShaderBackend backend) {
    sg::spike::ShaderBlob vertexBlob;
    sg::spike::ShaderBlob fragmentBlob;
    if (!asset.find(sg::spike::ShaderStage::Vertex, backend, vertexBlob) ||
        !asset.find(sg::spike::ShaderStage::Fragment, backend, fragmentBlob)) {
        HBG_LOGE("SG-0 spike: .sgb is missing vertex/fragment blobs for the active backend");
        return false;
    }
    spike.program = resources.createProgram(vertexBlob.data, vertexBlob.size, fragmentBlob.data,
                                            fragmentBlob.size, "sg.spike.forward");
    if (!spike.program.valid()) {
        HBG_LOGE("SG-0 spike: program creation failed");
        return false;
    }

    std::vector<SpikeVertex> vertices;
    std::vector<std::uint16_t> indices;
    buildSphere(vertices, indices);
    spike.indexCount = static_cast<std::uint32_t>(indices.size());

    gfx::VertexLayout layout;
    layout.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    gfx::BufferDesc vertexDesc;
    vertexDesc.type = gfx::BufferType::Vertex;
    vertexDesc.data = vertices.data();
    vertexDesc.size = static_cast<std::uint32_t>(vertices.size() * sizeof(SpikeVertex));
    vertexDesc.count = static_cast<std::uint32_t>(vertices.size());
    vertexDesc.layout = &layout;
    vertexDesc.name = "sg.spike.vertex";
    spike.vertexBuffer = resources.createBuffer(vertexDesc);

    gfx::BufferDesc indexDesc;
    indexDesc.type = gfx::BufferType::Index;
    indexDesc.data = indices.data();
    indexDesc.size = static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t));
    indexDesc.count = static_cast<std::uint32_t>(indices.size());
    indexDesc.name = "sg.spike.index";
    spike.indexBuffer = resources.createBuffer(indexDesc);

    const std::array<std::uint32_t, 64> shadowContext = {};
    gfx::BufferDesc shadowContextDesc;
    shadowContextDesc.type = gfx::BufferType::Storage;
    shadowContextDesc.data = shadowContext.data();
    shadowContextDesc.size = static_cast<std::uint32_t>(shadowContext.size() * sizeof(std::uint32_t));
    shadowContextDesc.count = static_cast<std::uint32_t>(shadowContext.size());
    shadowContextDesc.flags = BGFX_BUFFER_COMPUTE_READ;
    shadowContextDesc.name = "sg.spike.shadowContext";
    spike.shadowContext = resources.createBuffer(shadowContextDesc);

    const std::vector<std::uint8_t> checker = makeCheckerTexture();
    spike.mainTexture =
        createTexture(resources, "sg.spike.main", 256, 256, gfx::TextureFormat::RGBA8,
                      BGFX_SAMPLER_NONE, checker.data(),
                      static_cast<std::uint32_t>(checker.size()));

    const std::array<std::uint8_t, 4> white = {255, 255, 255, 255};
    constexpr std::uint32_t kPointClamp = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    for (std::size_t index = 0; index < spike.dummyTextures.size(); ++index) {
        const std::string name = "sg.spike.dummy" + std::to_string(index);
        spike.dummyTextures[index] =
            createTexture(resources, name.c_str(), 1, 1, gfx::TextureFormat::RGBA8, kPointClamp,
                          white.data(), static_cast<std::uint32_t>(white.size()));
    }

    const std::array<std::uint32_t, 1> zeroUint = {0};
    spike.lightList = createTexture(resources, "sg.spike.lightList", 1, 1, gfx::TextureFormat::R32U,
                                    kPointClamp, zeroUint.data(),
                                    static_cast<std::uint32_t>(zeroUint.size() * sizeof(std::uint32_t)));
    spike.shadowMap =
        createTexture(resources, "sg.spike.shadowMap", 1, 1, gfx::TextureFormat::D32F, kPointClamp,
                      nullptr, 0);
    spike.punctualShadow = createTexture(resources, "sg.spike.punctualShadow", 1, 1,
                                        gfx::TextureFormat::D32F, kPointClamp, nullptr, 0);
    spike.environment = createTexture(resources, "sg.spike.environment", 1, 1,
                                      gfx::TextureFormat::RGBA8, BGFX_SAMPLER_V_CLAMP, white.data(),
                                      static_cast<std::uint32_t>(white.size()));
    spike.contactShadow = createTexture(resources, "sg.spike.contactShadow", 1, 1,
                                        gfx::TextureFormat::RGBA8, kPointClamp, white.data(),
                                        static_cast<std::uint32_t>(white.size()));
    spike.probeMaps = createTexture(resources, "sg.spike.probeMaps", 1, 1,
                                    gfx::TextureFormat::RGBA8, BGFX_SAMPLER_V_CLAMP, white.data(),
                                    static_cast<std::uint32_t>(white.size()));
    spike.cookieAtlas = createTexture(resources, "sg.spike.cookieAtlas", 1, 1,
                                      gfx::TextureFormat::RGBA8, kPointClamp, white.data(),
                                      static_cast<std::uint32_t>(white.size()));

    std::array<float, 64 * 4> lightData = {};
    const Vec3 lightDirection = normalize(Vec3(0.35f, 0.62f, -0.70f));
    const auto setTexel = [&lightData](std::uint32_t index, float x, float y, float z, float w) {
        lightData[index * 4 + 0] = x;
        lightData[index * 4 + 1] = y;
        lightData[index * 4 + 2] = z;
        lightData[index * 4 + 3] = w;
    };
    setTexel(0, 0.0f, 0.0f, 0.0f, 0.0f);
    setTexel(1, 1.0f, 1.0f, 1.0f, 4.0f);
    setTexel(2, lightDirection.x, lightDirection.y, lightDirection.z, 0.0f);
    setTexel(3, -1.0f, 0.0f, 0.0f, 0.0f);
    setTexel(4, 0.0f, 0.0f, 0.0f, 1.0f);
    setTexel(5, 0.0f, 0.0f, 0.0f, 0.0f);
    setTexel(6, 65535.0f, 0.0f, 4.0f, 0.0f);
    spike.lightData = createTexture(resources, "sg.spike.lightData", 64, 1,
                                    gfx::TextureFormat::RGBA32F, kPointClamp, lightData.data(),
                                    static_cast<std::uint32_t>(lightData.size() * sizeof(float)));

    gfx::TextureDesc colorDesc;
    colorDesc.width = kSpikeSize;
    colorDesc.height = kSpikeSize;
    colorDesc.format = gfx::TextureFormat::RGBA8;
    colorDesc.renderTarget = true;
    colorDesc.name = "sg.spike.color";
    spike.color = resources.createTexture(colorDesc);

    gfx::TextureDesc depthDesc;
    depthDesc.width = kSpikeSize;
    depthDesc.height = kSpikeSize;
    depthDesc.format = gfx::TextureFormat::D24S8;
    depthDesc.depthStencil = true;
    depthDesc.name = "sg.spike.depth";
    spike.depth = resources.createTexture(depthDesc);

    gfx::TextureDesc readbackDesc;
    readbackDesc.width = kSpikeSize;
    readbackDesc.height = kSpikeSize;
    readbackDesc.format = gfx::TextureFormat::RGBA8;
    readbackDesc.flags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK;
    readbackDesc.name = "sg.spike.readback";
    spike.readback = resources.createTexture(readbackDesc);

    gfx::FrameBufferKey frameBufferKey;
    frameBufferKey.attachments = {spike.color, spike.depth};
    spike.frameBuffer = resources.createFrameBuffer(frameBufferKey, "sg.spike.frameBuffer");

    spike.mainSampler = resources.createUniform({"s_sg_MainTex", gfx::UniformType::Sampler, 1});
    for (std::size_t index = 0; index < spike.dummySamplers.size(); ++index) {
        const std::string name = "s_sg_Dummy" + std::to_string(index + 1);
        spike.dummySamplers[index] = resources.createUniform({name, gfx::UniformType::Sampler, 1});
    }
    spike.lightDataSampler = resources.createUniform({"s_lightData", gfx::UniformType::Sampler, 1});
    spike.shadowMapSampler = resources.createUniform({"s_shadowMap", gfx::UniformType::Sampler, 1});
    spike.lightListSampler = resources.createUniform({"s_lightList", gfx::UniformType::Sampler, 1});
    spike.punctualShadowSampler =
        resources.createUniform({"s_shadowMapPunctual", gfx::UniformType::Sampler, 1});
    spike.environmentSampler =
        resources.createUniform({"s_environmentMap", gfx::UniformType::Sampler, 1});
    spike.contactShadowSampler =
        resources.createUniform({"s_contactShadow", gfx::UniformType::Sampler, 1});
    spike.probeSampler = resources.createUniform({"s_probeMaps", gfx::UniformType::Sampler, 1});
    spike.cookieSampler = resources.createUniform({"s_cookieAtlas", gfx::UniformType::Sampler, 1});
    spike.tintUniform = resources.createUniform({"u_sg_Tint", gfx::UniformType::Vec4, 1});
    spike.smoothnessUniform = resources.createUniform({"u_sg_Smoothness", gfx::UniformType::Vec4, 1});
    spike.debugTexturesUniform =
        resources.createUniform({"u_sg_DebugTextures", gfx::UniformType::Vec4, 1});

    bool valid = spike.program.valid() && spike.vertexBuffer.valid() && spike.indexBuffer.valid() &&
                 spike.shadowContext.valid() && spike.color.valid() && spike.depth.valid() &&
                 spike.readback.valid() && spike.mainTexture.valid() && spike.lightData.valid() &&
                 spike.shadowMap.valid() && spike.punctualShadow.valid() && spike.lightList.valid() &&
                 spike.environment.valid() && spike.contactShadow.valid() && spike.probeMaps.valid() &&
                 spike.cookieAtlas.valid() && spike.frameBuffer.valid() && spike.mainSampler.valid() &&
                 spike.lightDataSampler.valid() && spike.shadowMapSampler.valid() &&
                 spike.lightListSampler.valid() && spike.punctualShadowSampler.valid() &&
                 spike.environmentSampler.valid() && spike.contactShadowSampler.valid() &&
                 spike.probeSampler.valid() && spike.cookieSampler.valid() && spike.tintUniform.valid() &&
                 spike.smoothnessUniform.valid() && spike.debugTexturesUniform.valid();
    for (const gfx::TextureHandle texture : spike.dummyTextures) {
        valid = valid && texture.valid();
    }
    for (const gfx::UniformHandle sampler : spike.dummySamplers) {
        valid = valid && sampler.valid();
    }
    if (!valid) {
        HBG_LOGE("SG-0 spike: resource creation failed");
        return false;
    }

    if (!spike.globalBinder.init(resources) || !spike.passBinder.init(resources) ||
        !spike.clusterBinder.init(resources) || !spike.environmentBinder.init(resources) ||
        !spike.probeBinder.init(resources) || !spike.contactBinder.init(resources) ||
        !spike.objectBinder.init(resources)) {
        HBG_LOGE("SG-0 spike: uniform binder initialization failed");
        return false;
    }

    const Mat4 identity = Mat4::identity();
    const Mat4 view =
        lookAtRH(Vec3(0.0f, 0.0f, -3.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 projection = perspectiveRH(
        1.0471975512f, static_cast<float>(kSpikeSize) / static_cast<float>(kSpikeSize), 0.1f, 100.0f,
        bgfx::getCaps()->homogeneousDepth);
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
    spike.globalUniforms.screenSize = Vec4(static_cast<float>(kSpikeSize),
                                           static_cast<float>(kSpikeSize),
                                           1.0f / static_cast<float>(kSpikeSize),
                                           1.0f / static_cast<float>(kSpikeSize));
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
    spike.passUniforms.lightDirection = Vec4(lightDirection.x, lightDirection.y, lightDirection.z, 0.0f);
    spike.passUniforms.lightColor = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    spike.passUniforms.ambientColor = Vec4(0.18f, 0.18f, 0.22f, 1.0f);
    spike.passUniforms.lightCounts = Vec4(1.0f, 1.0f, 0.0f, 0.0f);
    spike.passUniforms.lightListParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    spike.passUniforms.lightListParams2 = Vec4(33.0f, 32.0f, 0.0f, 0.0f);
    spike.passUniforms.lightScreenSize = spike.globalUniforms.screenSize;
    spike.passUniforms.cookieAtlasParams = Vec4(1.0f, 0.0f, 0.0f, 0.0f);
    spike.passUniforms.litVariantParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    spike.passUniforms.lightmapParams2 = Vec4(0.0f, 0.0f, 0.0f, 0.0f);

    spike.clusterUniforms.screenSize = spike.globalUniforms.screenSize;
    spike.clusterUniforms.depthParams = Vec4(0.0f, 0.0f, 1.0f, 0.0f);
    spike.environmentUniforms.skyParams = Vec4(1.0f, 0.0f, 0.0f, 0.0f);
    spike.objectUniforms.prevModel = identity;
    spike.objectUniforms.lightLayerMask = Vec4(65535.0f, 0.0f, 0.0f, 0.0f);
    spike.objectUniforms.lightmapST = Vec4(1.0f, 1.0f, 0.0f, 0.0f);
    spike.objectUniforms.lightmapParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    return true;
}

void destroyResources(SgSpike& spike, gfx::ResourceManager& resources) {
    spike.globalBinder.shutdown(resources);
    spike.passBinder.shutdown(resources);
    spike.clusterBinder.shutdown(resources);
    spike.environmentBinder.shutdown(resources);
    spike.probeBinder.shutdown(resources);
    spike.contactBinder.shutdown(resources);
    spike.objectBinder.shutdown(resources);

    const auto destroyUniform = [&resources](gfx::UniformHandle& handle) {
        if (handle.valid()) {
            resources.destroyUniform(handle);
            handle = {};
        }
    };
    destroyUniform(spike.mainSampler);
    for (gfx::UniformHandle& sampler : spike.dummySamplers) {
        destroyUniform(sampler);
    }
    destroyUniform(spike.lightDataSampler);
    destroyUniform(spike.shadowMapSampler);
    destroyUniform(spike.lightListSampler);
    destroyUniform(spike.punctualShadowSampler);
    destroyUniform(spike.environmentSampler);
    destroyUniform(spike.contactShadowSampler);
    destroyUniform(spike.probeSampler);
    destroyUniform(spike.cookieSampler);
    destroyUniform(spike.tintUniform);
    destroyUniform(spike.smoothnessUniform);
    destroyUniform(spike.debugTexturesUniform);

    if (spike.frameBuffer.valid()) {
        resources.destroyFrameBuffer(spike.frameBuffer);
        spike.frameBuffer = {};
    }
    if (spike.program.valid()) {
        resources.destroyProgram(spike.program);
        spike.program = {};
    }
    const auto destroyBuffer = [&resources](gfx::BufferHandle& handle) {
        if (handle.valid()) {
            resources.destroyBuffer(handle);
            handle = {};
        }
    };
    destroyBuffer(spike.vertexBuffer);
    destroyBuffer(spike.indexBuffer);
    destroyBuffer(spike.shadowContext);
    const auto destroyTexture = [&resources](gfx::TextureHandle& handle) {
        if (handle.valid()) {
            resources.destroyTexture(handle);
            handle = {};
        }
    };
    destroyTexture(spike.color);
    destroyTexture(spike.depth);
    destroyTexture(spike.readback);
    destroyTexture(spike.mainTexture);
    for (gfx::TextureHandle& texture : spike.dummyTextures) {
        destroyTexture(texture);
    }
    destroyTexture(spike.lightData);
    destroyTexture(spike.shadowMap);
    destroyTexture(spike.punctualShadow);
    destroyTexture(spike.lightList);
    destroyTexture(spike.environment);
    destroyTexture(spike.contactShadow);
    destroyTexture(spike.probeMaps);
    destroyTexture(spike.cookieAtlas);
    resources.flushPending(true);
}

void bindFrameUniforms(gfx::Encoder& encoder, const SgSpike& spike) {
    spike.globalBinder.upload(spike.globalUniforms);
    spike.passBinder.upload(spike.passUniforms);
    spike.clusterBinder.upload(spike.clusterUniforms);
    spike.environmentBinder.upload(spike.environmentUniforms);
    spike.probeBinder.upload(spike.probeUniforms);
    spike.contactBinder.upload(spike.contactUniforms);
    spike.objectBinder.upload(spike.objectUniforms);
    encoder.setUniform(spike.tintUniform, &spike.tint, 1);
    encoder.setUniform(spike.smoothnessUniform, &spike.smoothness, 1);
    encoder.setUniform(spike.debugTexturesUniform, &spike.debugTextures, 1);
}

void bindTextures(gfx::Encoder& encoder, const SgSpike& spike) {
    constexpr std::uint32_t kPointClamp = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    encoder.setTexture(0, spike.mainSampler, spike.mainTexture, BGFX_SAMPLER_NONE);
    for (std::size_t index = 0; index < spike.dummySamplers.size(); ++index) {
        encoder.setTexture(static_cast<std::uint8_t>(1 + index), spike.dummySamplers[index],
                           spike.dummyTextures[index], kPointClamp);
    }
    encoder.setTexture(5, spike.lightDataSampler, spike.lightData, kPointClamp);
    encoder.setTexture(6, spike.shadowMapSampler, spike.shadowMap, kPointClamp);
    encoder.setTexture(7, spike.lightListSampler, spike.lightList, kPointClamp);
    encoder.setStorageBuffer(8, spike.shadowContext, bgfx::Access::Read);
    encoder.setTexture(9, spike.punctualShadowSampler, spike.punctualShadow, kPointClamp);
    encoder.setTexture(10, spike.environmentSampler, spike.environment, BGFX_SAMPLER_V_CLAMP);
    encoder.setTexture(11, spike.contactShadowSampler, spike.contactShadow, kPointClamp);
    encoder.setTexture(14, spike.probeSampler, spike.probeMaps, BGFX_SAMPLER_V_CLAMP);
    encoder.setTexture(15, spike.cookieSampler, spike.cookieAtlas, kPointClamp);
}

void renderFrame(gfx::ResourceManager& resources, const SgSpike& spike) {
    gfx::Encoder encoder(resources);
    encoder.setViewRect(kSpikeView, 0, 0, kSpikeSize, kSpikeSize);
    encoder.setViewFrameBuffer(kSpikeView, spike.frameBuffer);
    encoder.setViewClear(kSpikeView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x181820ff, 1.0f, 0);
    encoder.setViewTransform(kSpikeView, spike.globalUniforms.view.values.data(),
                             spike.globalUniforms.proj.values.data());
    bindFrameUniforms(encoder, spike);
    bindTextures(encoder, spike);
    encoder.setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS);
    encoder.setTransform(spike.model.values.data());
    encoder.setVertexBuffer(0, spike.vertexBuffer);
    encoder.setIndexBuffer(spike.indexBuffer);
    encoder.submit(kSpikeView, spike.program);
    bgfx::frame();
    resources.endFrame();
}

bool readColor(gfx::ResourceManager& resources, const SgSpike& spike,
               std::vector<std::uint8_t>& pixels) {
    bgfx::setViewRect(kSpikeBlitView, 0, 0, kSpikeSize, kSpikeSize);
    bgfx::blit(kSpikeBlitView,
               {.handle = resources.native(spike.readback), .width = kSpikeSize, .height = kSpikeSize},
               {.handle = resources.native(spike.color), .width = kSpikeSize, .height = kSpikeSize});
    bgfx::touch(kSpikeBlitView);

    pixels.resize(static_cast<std::size_t>(kSpikeSize) * kSpikeSize * 4);
    const std::uint32_t expectedFrame =
        bgfx::read({.handle = resources.native(spike.readback),
                    .width = kSpikeSize,
                    .height = kSpikeSize},
                   pixels.data());
    if (expectedFrame == 0) {
        HBG_LOGE("SG-0 spike: color readback request failed");
        return false;
    }
    std::uint32_t currentFrame = bgfx::frame();
    resources.endFrame();
    while (currentFrame < expectedFrame) {
        currentFrame = bgfx::frame();
        resources.endFrame();
    }
    return true;
}

struct ImageStats {
    double mean = 0.0;
    std::uint32_t nonBackground = 0;
};

ImageStats analyzePixels(const std::vector<std::uint8_t>& pixels) {
    ImageStats stats;
    double sum = 0.0;
    std::uint32_t count = 0;
    for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
        const int dr = std::abs(static_cast<int>(pixels[index]) - 0x18);
        const int dg = std::abs(static_cast<int>(pixels[index + 1]) - 0x18);
        const int db = std::abs(static_cast<int>(pixels[index + 2]) - 0x20);
        if (dr > 3 || dg > 3 || db > 3) {
            sum += (static_cast<double>(pixels[index]) + pixels[index + 1] + pixels[index + 2]) /
                   (3.0 * 255.0);
            ++count;
        }
    }
    stats.nonBackground = count;
    stats.mean = count > 0 ? sum / static_cast<double>(count) : 0.0;
    return stats;
}

bool saveScreenshot(const std::string& path, const std::vector<std::uint8_t>& pixels) {
    if (path.empty()) {
        return true;
    }
    std::error_code error;
    const std::filesystem::path output(path);
    if (output.has_parent_path()) {
        std::filesystem::create_directories(output.parent_path(), error);
    }
    if (!writeImageFile(path, pixels.data(), kSpikeSize, kSpikeSize, 92)) {
        HBG_LOGE("SG-0 spike: failed to write screenshot '{}'", path);
        return false;
    }
    HBG_LOGI("SG-0 spike: wrote screenshot '{}'", path);
    return true;
}

} // namespace

bool runSgSpike(gfx::ResourceManager& resources, std::uint16_t width, std::uint16_t height,
                const SgSpikeOptions& options) {
    (void)width;
    (void)height;
    const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    if (!isBackendSupported(rendererType)) {
        HBG_LOGE("SG-0 spike: unsupported backend '{}'", bgfx::getRendererName(rendererType));
        return false;
    }

    const std::string assetPath =
        options.assetPath.empty() ? std::string(HBG_SG_SPIKE_ASSET_DEFAULT) : options.assetPath;
    sg::spike::Asset asset;
    std::string error;
    if (!asset.load(assetPath, error)) {
        HBG_LOGE("SG-0 spike: failed to load '{}': {}", assetPath, error);
        return false;
    }

    const sg::spike::ShaderBackend backend = backendForRenderer(rendererType);
    HBG_LOGI("SG-0 spike: backend={} asset='{}' stageBudget={} contentHash=0x{:016x}",
             bgfx::getRendererName(rendererType), assetPath, kGraphStageBudget, asset.contentHash());

    SgSpike spike;
    bool ok = createResources(spike, resources, asset, backend);
    if (!ok) {
        destroyResources(spike, resources);
        return false;
    }

    SgMjpegServer server;
    if (options.preview) {
        const std::string token = options.token.empty() ? makeToken() : options.token;
        if (!server.start(options.port, token, error)) {
            HBG_LOGE("SG-0 spike: preview server failed: {}", error);
            destroyResources(spike, resources);
            return false;
        }
        HBG_LOGI("SG-0 spike: preview at http://127.0.0.1:{}/?token={}", options.port, token);
    }

    std::uint32_t frames = options.frames;
    if (options.preview && !options.framesSpecified) {
        frames = 600;
    }
    if (frames == 0) {
        frames = options.preview ? 600 : 1;
    }

    double totalRenderMs = 0.0;
    double totalReadbackMs = 0.0;
    double totalJpegMs = 0.0;
    std::uint32_t readbackFrames = 0;
    double firstReadbackMs = 0.0;
    std::uint32_t encodedFrames = 0;
    std::uint32_t lastNonBackground = 0;
    double lastMean = 0.0;
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> jpeg;

    for (std::uint32_t frameIndex = 0; frameIndex < frames; ++frameIndex) {
        const auto renderStart = std::chrono::steady_clock::now();
        renderFrame(resources, spike);
        const auto renderEnd = std::chrono::steady_clock::now();
        totalRenderMs += std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();

        const bool finalFrame = frameIndex + 1 >= frames;
        const bool needReadback = options.preview || finalFrame;
        if (!needReadback) {
            continue;
        }

        if (!readColor(resources, spike, pixels)) {
            ok = false;
            break;
        }
        const auto readbackEnd = std::chrono::steady_clock::now();
        if (readbackFrames == 0) {
            firstReadbackMs = std::chrono::duration<double, std::milli>(readbackEnd - renderEnd).count();
        }
        ++readbackFrames;
        const ImageStats stats = analyzePixels(pixels);
        lastNonBackground = stats.nonBackground;
        lastMean = stats.mean;

        double jpegMs = 0.0;
        if (options.preview) {
            const auto jpegStart = std::chrono::steady_clock::now();
            if (!encodeJpeg(pixels.data(), kSpikeSize, kSpikeSize, 85, jpeg)) {
                HBG_LOGE("SG-0 spike: JPEG encode failed");
                ok = false;
                break;
            }
            server.publish(std::move(jpeg));
            const auto jpegEnd = std::chrono::steady_clock::now();
            jpegMs = std::chrono::duration<double, std::milli>(jpegEnd - jpegStart).count();
            totalJpegMs += jpegMs;
            ++encodedFrames;
        }

        if (!options.screenshotPath.empty() && (finalFrame || (options.preview && frameIndex == 0))) {
            if (!saveScreenshot(options.screenshotPath, pixels)) {
                ok = false;
                break;
            }
        }

        const double renderMs =
            std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();
        const double readbackMs =
            std::chrono::duration<double, std::milli>(readbackEnd - renderEnd).count();
        totalReadbackMs += readbackMs;
        if (options.preview || finalFrame) {
            HBG_LOGI("SG-0 spike frame {}: render={:.2f}ms readback={:.2f}ms jpeg={:.2f}ms "
                     "mean={:.4f} nonBackground={}",
                     frameIndex, renderMs, readbackMs, jpegMs, lastMean, lastNonBackground);
        }
        if (options.preview && !finalFrame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    if (ok && lastNonBackground < 1000) {
        HBG_LOGE("SG-0 spike: rendered image is empty (nonBackground={})", lastNonBackground);
        ok = false;
    }
    if (ok) {
        const double measuredFrames = static_cast<double>(readbackFrames);
        if (measuredFrames > 0.0) {
            const double steadyReadback =
                readbackFrames > 1 ? (totalReadbackMs - firstReadbackMs) / static_cast<double>(readbackFrames - 1)
                                   : totalReadbackMs;
            HBG_LOGI("SG-0 spike: avg render={:.2f}ms readback={:.2f}ms steady_readback={:.2f}ms "
                     "jpeg={:.2f}ms readback_frames={} mean={:.4f}",
                     totalRenderMs / static_cast<double>(frames), totalReadbackMs / measuredFrames,
                     steadyReadback, encodedFrames > 0 ? totalJpegMs / encodedFrames : 0.0,
                     readbackFrames, lastMean);
        }
    }

    server.stop();
    destroyResources(spike, resources);
    const std::size_t live = resources.liveResourceCount();
    if (live != 0) {
        HBG_LOGE("SG-0 spike: leaked {} resources", live);
        ok = false;
    }
    if (ok) {
        HBG_LOGI("SG-0 spike passed: graph stages 0-4 + engine stages 5,6,7,8,9,10,11,14,15");
    }
    return ok;
}

} // namespace hbg
