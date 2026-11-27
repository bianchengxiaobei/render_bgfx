#include "render/terrain/terrain_material_tile_streamer.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>

#ifndef HBG_SHADER_ROOT
#define HBG_SHADER_ROOT ""
#endif

namespace hbg::render {
namespace {

constexpr uint32_t kMaxQueuedTiles = 1024;
constexpr uint32_t kChannels = 4;
constexpr uint32_t kTileBorder = 2;
constexpr uint32_t kOrthoViewBase = 3000;
constexpr uint32_t kOrthoViewCount = 128;
constexpr uint32_t kTileMapWidth = 4096;
constexpr uint32_t kTileMapModulus = 4093;

struct OrthoVertex {
    float position[3];
};

constexpr OrthoVertex kOrthoVertices[] = {
    {{-1.0f, -1.0f, 0.0f}},
    {{3.0f, -1.0f, 0.0f}},
    {{-1.0f, 3.0f, 0.0f}},
};

uint8_t linearToSrgb8(uint8_t value) {
    const float linear = static_cast<float>(value) / 255.0f;
    const float srgb = linear <= 0.0031308f ? 12.92f * linear
                                            : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::clamp(std::lround(srgb * 255.0f), 0l, 255l));
}

float hashGrid(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffffu) / 8388607.5f - 1.0f;
}

float valueNoise(float x, float y) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const float tx = x - fx;
    const float ty = y - fy;
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sy = ty * ty * (3.0f - 2.0f * ty);
    const float n00 = hashGrid(ix, iy);
    const float n10 = hashGrid(ix + 1, iy);
    const float n01 = hashGrid(ix, iy + 1);
    const float n11 = hashGrid(ix + 1, iy + 1);
    return (n00 + (n10 - n00) * sx) + ((n01 + (n11 - n01) * sx) - (n00 + (n10 - n00) * sx)) * sy;
}

uint32_t orthoRandom(uint32_t& seed) {
    seed = (seed * 1103515245u + 12345u) & 0x7fffffffu;
    return seed;
}

float orthoFrandom(uint32_t& seed) {
    return static_cast<float>(orthoRandom(seed) >> 7) / 16777216.0f;
}

std::vector<uint8_t> createOrthoNoisePixels(uint32_t tileWidth) {
    constexpr uint32_t kNoiseLayers = 6;
    constexpr int kLayerSelection[kNoiseLayers] = {0, 1, 3, 5, 7, 15};
    const uint32_t width = std::max(tileWidth, 16u);
    std::vector<uint8_t> pixels(static_cast<std::size_t>(width) * width * kChannels * kNoiseLayers);
    uint32_t random = 1234567u;
    for (uint32_t layer = 0; layer < kNoiseLayers; ++layer) {
        uint8_t* base = pixels.data() + static_cast<std::size_t>(layer) * width * width * kChannels;
        const auto put = [&](uint32_t x, uint32_t y, uint8_t value) {
            uint8_t* texel = base + (static_cast<std::size_t>(y) * width + x) * kChannels;
            texel[0] = value;
            texel[1] = value;
            texel[2] = value;
            texel[3] = value;
        };
        for (uint32_t y = 0; y < width; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                put(x, y, 128);
            }
        }

        const int layerBits = kLayerSelection[layer];
        uint32_t borderRandom = (layerBits & 1) == 0 ? 7654321u : 5647381u;
        for (uint32_t y = 2; y < 4; ++y) {
            for (uint32_t x = 4; x < width - 4; ++x) {
                const uint8_t value = static_cast<uint8_t>(orthoFrandom(borderRandom) * 255.0f);
                put(x, y, value);
                put(width - 1 - x, 3 - y, value);
            }
        }

        borderRandom = (layerBits & 2) == 0 ? 7654321u : 5647381u;
        for (uint32_t x = width - 3; x >= width - 4; --x) {
            for (uint32_t y = 4; y < width - 4; ++y) {
                const uint8_t value = static_cast<uint8_t>(orthoFrandom(borderRandom) * 255.0f);
                put(x, y, value);
                put(2 * width - 5 - x, width - 1 - y, value);
            }
        }

        borderRandom = (layerBits & 4) == 0 ? 7654321u : 5647381u;
        for (uint32_t y = width - 2; y < width; ++y) {
            for (uint32_t x = 4; x < width - 4; ++x) {
                const uint8_t value = static_cast<uint8_t>(orthoFrandom(borderRandom) * 255.0f);
                put(x, y, value);
                put(width - 1 - x, 2 * width - 5 - y, value);
            }
        }

        borderRandom = (layerBits & 8) == 0 ? 7654321u : 5647381u;
        for (int x = 1; x >= 0; --x) {
            for (uint32_t y = 4; y < width - 4; ++y) {
                const uint8_t value = static_cast<uint8_t>(orthoFrandom(borderRandom) * 255.0f);
                put(static_cast<uint32_t>(x), y, value);
                put(static_cast<uint32_t>(3 - x), width - 1 - y, value);
            }
        }

        for (uint32_t y = 4; y < width - 4; ++y) {
            for (uint32_t x = 4; x < width - 4; ++x) {
                put(x, y, static_cast<uint8_t>(orthoFrandom(random) * 255.0f));
            }
        }
    }
    return pixels;
}

int orthoPermutation[514];
float orthoGradients[514][2];
bool orthoNoiseInitialized = false;

void initOrthoNoise() {
    if (orthoNoiseInitialized) {
        return;
    }
    uint32_t seed = 12345u;
    for (int i = 0; i < 256; ++i) {
        orthoPermutation[i] = i;
        orthoRandom(seed);
        orthoGradients[i][0] =
            static_cast<float>(static_cast<int>(orthoRandom(seed) % 512u) - 256) / 256.0f;
        orthoGradients[i][1] =
            static_cast<float>(static_cast<int>(orthoRandom(seed) % 512u) - 256) / 256.0f;
        const float length = std::sqrt(orthoGradients[i][0] * orthoGradients[i][0] +
                                       orthoGradients[i][1] * orthoGradients[i][1]);
        if (length > 0.0f) {
            orthoGradients[i][0] /= length;
            orthoGradients[i][1] /= length;
        }
        orthoRandom(seed);
        orthoRandom(seed);
        orthoRandom(seed);
    }
    for (int i = 255; i > 0; --i) {
        const int j = static_cast<int>(orthoRandom(seed) % 256u);
        std::swap(orthoPermutation[i], orthoPermutation[j]);
    }
    for (int i = 0; i < 258; ++i) {
        orthoPermutation[256 + i] = orthoPermutation[i];
        orthoGradients[256 + i][0] = orthoGradients[i][0];
        orthoGradients[256 + i][1] = orthoGradients[i][1];
    }
    orthoNoiseInitialized = true;
}

float orthoCnoise(float x, float y) {
    initOrthoNoise();
    const auto setup = [](float value, int& b0, int& b1, float& r0, float& r1) {
        const float t = value + 0x1000;
        b0 = static_cast<int>(t) & 0xFF;
        b1 = (b0 + 1) & 0xFF;
        r0 = t - std::floor(t);
        r1 = r0 - 1.0f;
    };
    int bx0 = 0;
    int bx1 = 0;
    int by0 = 0;
    int by1 = 0;
    float rx0 = 0.0f;
    float rx1 = 0.0f;
    float ry0 = 0.0f;
    float ry1 = 0.0f;
    setup(x, bx0, bx1, rx0, rx1);
    setup(y, by0, by1, ry0, ry1);

    const int i = orthoPermutation[bx0];
    const int j = orthoPermutation[bx1];
    const int b00 = orthoPermutation[i + by0];
    const int b10 = orthoPermutation[j + by0];
    const int b01 = orthoPermutation[i + by1];
    const int b11 = orthoPermutation[j + by1];
    const float sx = rx0 * rx0 * (3.0f - 2.0f * rx0);
    const float sy = ry0 * ry0 * (3.0f - 2.0f * ry0);
    const auto at = [](const float* gradient, float rx, float ry) {
        return rx * gradient[0] + ry * gradient[1];
    };
    const float u0 = at(orthoGradients[b00], rx0, ry0);
    const float v0 = at(orthoGradients[b10], rx1, ry0);
    const float a = u0 + sx * (v0 - u0);
    const float u1 = at(orthoGradients[b01], rx0, ry1);
    const float v1 = at(orthoGradients[b11], rx1, ry1);
    const float b = u1 + sx * (v1 - u1);
    return a + sy * (b - a);
}

struct OrthoNoiseSelection {
    int uvX = 0;
    int uvY = 1;
    int layer = 0;
};

OrthoNoiseSelection selectOrthoNoise(int level, int tx, int ty) {
    const int offset = 1 << std::max(level, 0);
    const int bottom = orthoCnoise(static_cast<float>(tx) + 0.5f, static_cast<float>(ty + offset)) > 0.0f ? 1 : 0;
    const int right = (tx == offset - 1
                           ? orthoCnoise(static_cast<float>(ty + offset) + 0.5f, static_cast<float>(offset))
                           : orthoCnoise(static_cast<float>(tx + 1), static_cast<float>(ty + offset) + 0.5f)) > 0.0f
                          ? 2
                          : 0;
    const int top = (ty == offset - 1
                         ? orthoCnoise(static_cast<float>(3 * offset - 1 - tx) + 0.5f, static_cast<float>(offset))
                         : orthoCnoise(static_cast<float>(tx) + 0.5f, static_cast<float>(ty + offset + 1))) > 0.0f
                        ? 4
                        : 0;
    const int left = (tx == 0
                          ? orthoCnoise(static_cast<float>(4 * offset - 1 - ty) + 0.5f, static_cast<float>(offset))
                          : orthoCnoise(static_cast<float>(tx), static_cast<float>(ty + offset) + 0.5f)) > 0.0f
                         ? 8
                         : 0;
    const int border = bottom + right + top + left;
    constexpr int kNoiseR[16] = {0, 0, 1, 0, 2, 0, 1, 0, 3, 3, 1, 3, 2, 2, 1, 0};
    constexpr int kNoiseL[16] = {0, 1, 1, 2, 1, 3, 2, 4, 1, 2, 3, 4, 2, 4, 4, 5};
    OrthoNoiseSelection selection;
    selection.uvX = kNoiseR[border];
    selection.uvY = (selection.uvX + 1) % 4;
    selection.layer = kNoiseL[border];
    return selection;
}

} // namespace

TerrainMaterialTileStreamer::~TerrainMaterialTileStreamer() {
    close();
}

bool TerrainMaterialTileStreamer::init(const asset::ImageAsset& albedo, const asset::ImageAsset& normal,
                                       float originX, float originZ, float worldSize, uint32_t tileSize,
                                       uint32_t capacity, const OrthoParams& ortho,
                                       gfx::ResourceManager& resources, std::string* error) {
    close();
    const auto imageBytes = [](const asset::ImageAsset& image) {
        return static_cast<std::size_t>(image.width) * image.height * 4u;
    };
    if (!albedo.valid() || !normal.valid() || albedo.format != gfx::TextureFormat::RGBA8 ||
        normal.format != gfx::TextureFormat::RGBA8 || albedo.pixels.size() < imageBytes(albedo) ||
        normal.pixels.size() < imageBytes(normal)) {
        if (error != nullptr) {
            *error = "terrain material tile streamer: albedo/normal images must be RGBA8";
        }
        return false;
    }
    if (!std::isfinite(worldSize) || worldSize <= 0.0f) {
        if (error != nullptr) {
            *error = "terrain material tile streamer: invalid world size";
        }
        return false;
    }
    tileSize = std::max(1u, tileSize);
    capacity = std::max(1u, capacity);
    if (static_cast<uint64_t>(capacity) * 2u > 65535u) {
        if (error != nullptr) {
            *error = "terrain material tile streamer: tile cache is too large";
        }
        return false;
    }

    m_albedo = &albedo;
    m_normal = &normal;
    m_originX = originX;
    m_originZ = originZ;
    m_worldSize = worldSize;
    m_tileSize = tileSize;
    m_contentSize = tileSize;
    m_textureSize = tileSize + 2u * kTileBorder;
    m_capacity = capacity;
    m_ortho = ortho;
    m_resources = &resources;
    m_tileMapEnabled = ortho.quadtree;
    m_tileMapDirty = false;
    m_tileMapCollision = false;
    m_tileMapLoggedEntries = 0xffffffffu;
    m_tileMapCameraX = 0.0f;
    m_tileMapCameraZ = 0.0f;
    m_tileMapSplitDistance = -1.0f;
    m_tileMapMaxLevel = -1;

    const bool gpuOrthoTarget = ortho.mode != OrthoMode::Baked;
    uint8_t mipCount = 1;
    for (uint32_t size = m_textureSize; size > 1u; size >>= 1u) {
        ++mipCount;
    }
    const uint64_t tileTextureFlags =
        BGFX_SAMPLER_UVW_CLAMP | BGFX_TEXTURE_SRGB | (gpuOrthoTarget ? BGFX_TEXTURE_BLIT_DST : 0);
    if (!m_gpu.init(resources, m_textureSize, capacity * 2u, gfx::TextureFormat::RGBA8, "terrain.material.tiles",
                    tileTextureFlags, mipCount, gpuOrthoTarget)) {
        m_resources = nullptr;
        return false;
    }
    if (m_tileMapEnabled) {
        m_tileMapPixels.assign(static_cast<std::size_t>(kTileMapWidth) * 2u, 0u);
        gfx::TextureDesc mapDesc;
        mapDesc.width = static_cast<uint16_t>(kTileMapWidth);
        mapDesc.height = 1;
        mapDesc.format = gfx::TextureFormat::RG8;
        mapDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        mapDesc.data = m_tileMapPixels.data();
        mapDesc.dataSize = static_cast<uint32_t>(m_tileMapPixels.size());
        mapDesc.name = "terrain.material.tileMap";
        m_tileMapTexture = resources.createTexture(mapDesc);
        if (!m_tileMapTexture.valid()) {
            HBG_LOGW("terrain material tile streamer: tileMap texture unavailable; using direct layers");
            m_tileMapEnabled = false;
            m_tileMapPixels.clear();
        } else {
            m_tileMapDirty = true;
        }
    }
    if (gpuOrthoTarget) {
        m_gpuOrtho = initGpuOrtho();
        if (!m_gpuOrtho) {
            HBG_LOGW("terrain material tile streamer: GPU ortho producer unavailable; using CPU tile source");
        }
    }

    m_slotUsed.assign(m_capacity, 0);
    m_freeSlots = m_capacity;
    m_loaded = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = false;
    }
    m_worker = std::thread([this]() { run(); });
    HBG_LOGI("terrain material tile streamer: albedo={}x{} normal={}x{} tile={} content={} border={} slots={} "
             "layers={} mips={} bytes={} ortho_mode={} ortho_strength={} gpu_ortho={} quadtree={}",
             m_albedo->width, m_albedo->height, m_normal->width, m_normal->height, m_tileSize, m_contentSize,
             kTileBorder, m_capacity, m_capacity * 2u, m_gpu.mipCount(), m_gpu.gpuBytes(),
             static_cast<int>(m_ortho.mode), m_ortho.strength, m_gpuOrtho, m_tileMapEnabled);
    return true;
}

bool TerrainMaterialTileStreamer::initGpuOrtho() {
    if (m_resources == nullptr || !m_gpu.valid() || m_ortho.mode == OrthoMode::Baked) {
        return false;
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr) {
        return false;
    }

    const bgfx::RendererType::Enum renderer = bgfx::getRendererType();
    const char* backend = nullptr;
    if (renderer == bgfx::RendererType::Direct3D11) {
        backend = "dxbc";
    } else if (renderer == bgfx::RendererType::Vulkan) {
        backend = "spirv";
    } else {
        return false;
    }

    const std::filesystem::path root(HBG_SHADER_ROOT);
    const std::filesystem::path shaderDir = root / "build" / backend;
    const std::string vertexPath = (shaderDir / "terrain_ortho.vs.bin").string();
    const std::string fragmentPath = (shaderDir / "terrain_ortho.fs.bin").string();
    if (vertexPath.empty() || fragmentPath.empty() || !std::filesystem::exists(vertexPath) ||
        !std::filesystem::exists(fragmentPath)) {
        HBG_LOGW("terrain material tile streamer: ortho shader binaries not found under '{}'",
                 shaderDir.string());
        return false;
    }

    m_orthoProgram = m_resources->createProgramFromFiles(vertexPath, fragmentPath, "terrain.ortho");
    if (!m_orthoProgram.valid()) {
        return false;
    }

    gfx::VertexLayout layout;
    layout.position().end();
    gfx::BufferDesc vertexDesc;
    vertexDesc.type = gfx::BufferType::Vertex;
    vertexDesc.data = kOrthoVertices;
    vertexDesc.size = sizeof(kOrthoVertices);
    vertexDesc.count = 3;
    vertexDesc.layout = &layout;
    vertexDesc.name = "terrain.ortho.fullscreen";
    m_orthoVertexBuffer = m_resources->createBuffer(vertexDesc);
    if (!m_orthoVertexBuffer.valid()) {
        shutdownGpuOrtho();
        return false;
    }

    m_orthoParentSampler = m_resources->createUniform({"s_orthoParent", gfx::UniformType::Sampler, 1});
    m_orthoSourceSampler = m_resources->createUniform({"s_orthoSource", gfx::UniformType::Sampler, 1});
    m_orthoNoiseSampler = m_resources->createUniform({"s_orthoNoise", gfx::UniformType::Sampler, 1});
    m_orthoTransformUniform = m_resources->createUniform({"u_orthoTransform", gfx::UniformType::Vec4, 1});
    m_orthoCoarseUniform = m_resources->createUniform({"u_orthoCoarse", gfx::UniformType::Vec4, 1});
    m_orthoInfoUniform = m_resources->createUniform({"u_orthoInfo", gfx::UniformType::Vec4, 1});
    m_orthoFlagsUniform = m_resources->createUniform({"u_orthoFlags", gfx::UniformType::Vec4, 1});
    m_orthoColorUniform = m_resources->createUniform({"u_orthoColor", gfx::UniformType::Vec4, 1});
    m_orthoRootUniform = m_resources->createUniform({"u_orthoRoot", gfx::UniformType::Vec4, 1});
    m_orthoNoiseUniform = m_resources->createUniform({"u_orthoNoise", gfx::UniformType::Vec4, 1});
    if (!m_orthoParentSampler.valid() || !m_orthoSourceSampler.valid() || !m_orthoNoiseSampler.valid() ||
        !m_orthoTransformUniform.valid() || !m_orthoCoarseUniform.valid() ||
        !m_orthoInfoUniform.valid() || !m_orthoFlagsUniform.valid() || !m_orthoColorUniform.valid() ||
        !m_orthoRootUniform.valid() || !m_orthoNoiseUniform.valid()) {
        shutdownGpuOrtho();
        return false;
    }

    const std::vector<uint8_t> noisePixels = createOrthoNoisePixels(m_textureSize);
    gfx::TextureDesc noiseDesc;
    noiseDesc.width = static_cast<uint16_t>(m_textureSize);
    noiseDesc.height = static_cast<uint16_t>(m_textureSize);
    noiseDesc.layers = 6;
    noiseDesc.format = gfx::TextureFormat::RGBA8;
    noiseDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
    noiseDesc.data = noisePixels.data();
    noiseDesc.dataSize = static_cast<uint32_t>(noisePixels.size());
    noiseDesc.name = "terrain.ortho.noise";
    m_orthoNoiseTexture = m_resources->createTexture(noiseDesc);
    if (!m_orthoNoiseTexture.valid()) {
        shutdownGpuOrtho();
        return false;
    }

    if (m_ortho.mode == OrthoMode::Detail) {
        gfx::TextureDesc sourceDesc;
        sourceDesc.width = static_cast<uint16_t>(m_albedo->width);
        sourceDesc.height = static_cast<uint16_t>(m_albedo->height);
        sourceDesc.format = gfx::TextureFormat::RGBA8;
        sourceDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_TEXTURE_SRGB;
        sourceDesc.data = m_albedo->pixels.data();
        sourceDesc.dataSize = static_cast<uint32_t>(m_albedo->width * m_albedo->height * kChannels);
        sourceDesc.name = "terrain.ortho.source";
        m_orthoSourceTexture = m_resources->createTexture(sourceDesc);
        if (!m_orthoSourceTexture.valid()) {
            shutdownGpuOrtho();
            return false;
        }
    }

    const bool autogenMips = (caps->formats[bgfx::TextureFormat::RGBA8] &
                              BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN) != 0;
    if (!autogenMips) {
        HBG_LOGW("terrain material tile streamer: RGBA8 mip autogeneration is not supported");
        shutdownGpuOrtho();
        return false;
    }

    gfx::TextureDesc scratchDesc;
    scratchDesc.width = static_cast<uint16_t>(m_textureSize);
    scratchDesc.height = static_cast<uint16_t>(m_textureSize);
    scratchDesc.mipCount = m_gpu.mipCount();
    scratchDesc.format = gfx::TextureFormat::RGBA8;
    scratchDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_TEXTURE_SRGB;
    scratchDesc.renderTarget = true;
    scratchDesc.name = "terrain.ortho.scratch";
    m_orthoScratchTexture = m_resources->createTexture(scratchDesc);
    if (!m_orthoScratchTexture.valid()) {
        shutdownGpuOrtho();
        return false;
    }

    bgfx::Attachment attachment;
    attachment.init(m_resources->native(m_orthoScratchTexture), bgfx::Access::Write, 0, 1, 0,
                    BGFX_ATTACHMENT_AUTO_GEN_MIPS);
    m_orthoScratchFramebuffer = bgfx::createFrameBuffer(1, &attachment, false);
    if (!bgfx::isValid(m_orthoScratchFramebuffer)) {
        shutdownGpuOrtho();
        return false;
    }

    HBG_LOGI("terrain material tile streamer: GPU ortho producer ready (backend={}, slots={})", backend,
             m_capacity);
    return true;
}

void TerrainMaterialTileStreamer::shutdownGpuOrtho() {
    if (bgfx::isValid(m_orthoScratchFramebuffer)) {
        bgfx::destroy(m_orthoScratchFramebuffer);
    }
    m_orthoScratchFramebuffer = bgfx::FrameBufferHandle{bgfx::kInvalidHandle};
    if (m_orthoScratchTexture.valid() && m_resources != nullptr) {
        m_resources->destroyTexture(m_orthoScratchTexture);
    }
    m_orthoScratchTexture = {};
    if (m_orthoSourceTexture.valid() && m_resources != nullptr) {
        m_resources->destroyTexture(m_orthoSourceTexture);
    }
    m_orthoSourceTexture = {};
    if (m_orthoNoiseTexture.valid() && m_resources != nullptr) {
        m_resources->destroyTexture(m_orthoNoiseTexture);
    }
    m_orthoNoiseTexture = {};
    if (m_orthoVertexBuffer.valid() && m_resources != nullptr) {
        m_resources->destroyBuffer(m_orthoVertexBuffer);
    }
    m_orthoVertexBuffer = {};
    if (m_orthoProgram.valid() && m_resources != nullptr) {
        m_resources->destroyProgram(m_orthoProgram);
    }
    m_orthoProgram = {};
    const auto destroyUniform = [this](gfx::UniformHandle& uniform) {
        if (uniform.valid() && m_resources != nullptr) {
            m_resources->destroyUniform(uniform);
        }
        uniform = {};
    };
    destroyUniform(m_orthoParentSampler);
    destroyUniform(m_orthoSourceSampler);
    destroyUniform(m_orthoNoiseSampler);
    destroyUniform(m_orthoTransformUniform);
    destroyUniform(m_orthoCoarseUniform);
    destroyUniform(m_orthoInfoUniform);
    destroyUniform(m_orthoFlagsUniform);
    destroyUniform(m_orthoColorUniform);
    destroyUniform(m_orthoRootUniform);
    destroyUniform(m_orthoNoiseUniform);
    m_gpuOrtho = false;
}

void TerrainMaterialTileStreamer::close() {
    if (m_worker.joinable()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
            m_queue.clear();
        }
        m_condition.notify_all();
        m_worker.join();
    }

    if (m_resources != nullptr) {
        shutdownGpuOrtho();
        if (m_tileMapTexture.valid()) {
            m_resources->destroyTexture(m_tileMapTexture);
        }
        m_tileMapTexture = {};
        m_gpu.shutdown(*m_resources);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_pending.clear();
        m_ready.clear();
        m_failed.clear();
        m_completed.clear();
        m_stop = false;
        m_stats = {};
    }
    m_lru.clear();
    m_lruIndex.clear();
    m_tileMapPixels.clear();
    m_tileMapEnabled = false;
    m_tileMapDirty = false;
    m_tileMapCollision = false;
    m_tileMapLoggedEntries = 0xffffffffu;
    m_tileMapCameraX = 0.0f;
    m_tileMapCameraZ = 0.0f;
    m_tileMapSplitDistance = -1.0f;
    m_tileMapMaxLevel = -1;
    m_slotUsed.clear();
    m_freeSlots = 0;
    m_loaded = false;
    m_resources = nullptr;
    m_albedo = nullptr;
    m_normal = nullptr;
    m_originX = 0.0f;
    m_originZ = 0.0f;
    m_worldSize = 0.0f;
    m_tileSize = 0;
    m_contentSize = 0;
    m_textureSize = 0;
    m_capacity = 0;
}

uint64_t TerrainMaterialTileStreamer::tileKey(int level, int tx, int ty) {
    return (static_cast<uint64_t>(level) << 32) | (static_cast<uint64_t>(tx) << 16) |
           static_cast<uint64_t>(ty);
}

uint32_t TerrainMaterialTileStreamer::tileMapKey(int level, int tx, int ty) {
    const uint64_t side = 1ull << static_cast<unsigned>(level);
    const uint64_t treeBase = ((1ull << static_cast<unsigned>(2 * level)) - 1ull) / 3ull;
    return static_cast<uint32_t>((static_cast<uint64_t>(tx) + static_cast<uint64_t>(ty) * side + treeBase) %
                                 kTileMapModulus);
}

void TerrainMaterialTileStreamer::refreshTileMap(float splitDistance, float cameraX, float cameraZ,
                                                 int maxLevel) {
    if (!m_tileMapEnabled || !m_tileMapTexture.valid() || m_resources == nullptr) {
        return;
    }

    const float safeSplit = std::isfinite(splitDistance) ? std::max(splitDistance, 0.0f) : 0.0f;
    const int k = static_cast<int>(std::ceil(safeSplit));
    const int maxLevelClamped = std::clamp(maxLevel, 0, 30);
    if (std::abs(cameraX - m_tileMapCameraX) > 0.01f || std::abs(cameraZ - m_tileMapCameraZ) > 0.01f ||
        std::abs(safeSplit - m_tileMapSplitDistance) > 1.0e-4f || maxLevelClamped != m_tileMapMaxLevel) {
        m_tileMapCameraX = cameraX;
        m_tileMapCameraZ = cameraZ;
        m_tileMapSplitDistance = safeSplit;
        m_tileMapMaxLevel = maxLevelClamped;
        m_tileMapDirty = true;
    }
    if (!m_tileMapDirty) {
        return;
    }

    std::fill(m_tileMapPixels.begin(), m_tileMapPixels.end(), uint8_t{0});
    uint32_t entries = 0;

    const auto layerFor = [&](int level, int tx, int ty) -> uint32_t {
        const int side = 1 << level;
        if (tx < 0 || ty < 0 || tx >= side || ty >= side) {
            return ~0u;
        }
        const auto found = m_ready.find(tileKey(level, tx, ty));
        return found == m_ready.end() ? ~0u : found->second * 2u;
    };

    const auto writeHashEntry = [&](uint32_t index, uint32_t layer) {
        if (index >= kTileMapWidth || layer == ~0u) {
            return;
        }
        const uint8_t x = static_cast<uint8_t>(layer & 0xffu);
        const uint8_t y = static_cast<uint8_t>((layer >> 8) + 1u);
        uint8_t* slot = m_tileMapPixels.data() + static_cast<std::size_t>(index) * 2u;
        if (slot[1] != 0) {
            if (slot[0] == x && slot[1] == y) {
                return;
            }
            if (!m_tileMapCollision) {
                m_tileMapCollision = true;
                HBG_LOGW("terrain material tile map: hash collision at {}; colliding entries fall back to "
                         "direct layers",
                         index);
            }
            slot[0] = 0;
            slot[1] = 0;
            return;
        }
        slot[0] = x;
        slot[1] = y;
        ++entries;
    };

    if (k > 2) {
        for (int level = 0; level <= maxLevelClamped; ++level) {
            const float tileSize = m_worldSize / static_cast<float>(1 << level);
            const float tileTwice = std::max(tileSize * 2.0f, 1.0e-4f);
            const int tx0 = static_cast<int>(std::floor(cameraX / tileTwice));
            const int ty0 = static_cast<int>(std::floor(cameraZ / tileTwice));
            for (int ty = 2 * (ty0 - k); ty <= 2 * (ty0 + k) + 1; ++ty) {
                for (int tx = 2 * (tx0 - k); tx <= 2 * (tx0 + k) + 1; ++tx) {
                    const uint32_t layer = layerFor(level, tx, ty);
                    writeHashEntry(tileMapKey(level, tx, ty), layer);
                }
            }
        }
    } else {
        uint32_t n = 0;
        for (int level = 0; level <= maxLevelClamped; ++level) {
            const float tileSize = m_worldSize / static_cast<float>(1 << level);
            const float tileTwice = std::max(tileSize * 2.0f, 1.0e-4f);
            const int tx0 = static_cast<int>(std::floor(cameraX / tileTwice));
            const int ty0 = static_cast<int>(std::floor(cameraZ / tileTwice));
            for (int ty = 2 * (ty0 - k); ty <= 2 * (ty0 + k) + 1; ++ty) {
                for (int tx = 2 * (tx0 - k); tx <= 2 * (tx0 + k) + 1; ++tx) {
                    if (n >= kTileMapWidth) {
                        break;
                    }
                    const uint32_t layer = layerFor(level, tx, ty);
                    uint8_t* slot = m_tileMapPixels.data() + static_cast<std::size_t>(n) * 2u;
                    if (layer != ~0u) {
                        slot[0] = static_cast<uint8_t>(layer & 0xffu);
                        slot[1] = static_cast<uint8_t>((layer >> 8) + 1u);
                        ++entries;
                    }
                    ++n;
                }
            }
        }
    }

    if (!m_resources->updateTexture(m_tileMapTexture, m_tileMapPixels.data(),
                                    static_cast<uint32_t>(m_tileMapPixels.size()),
                                    static_cast<uint16_t>(kTileMapWidth), 1,
                                    static_cast<uint16_t>(kTileMapWidth * 2u))) {
        HBG_LOGW("terrain material tile map: update failed");
        return;
    }
    m_tileMapDirty = false;
    if (entries > 0 && m_tileMapLoggedEntries != entries) {
        m_tileMapLoggedEntries = entries;
        HBG_LOGI("terrain material tile map: entries={} layout={} width={} camera=({}, {}) k={}", entries,
                 k > 2 ? "hash" : "local", kTileMapWidth, cameraX, cameraZ, k);
    }
}

bool TerrainMaterialTileStreamer::request(int level, int tx, int ty) {
    if (!m_loaded || m_worldSize <= 0.0f || level < 0 || level >= 31) {
        return false;
    }
    const int count = 1 << level;
    if (tx < 0 || ty < 0 || tx >= count || ty >= count) {
        return false;
    }

    const uint64_t key = tileKey(level, tx, ty);
    const auto ready = m_ready.find(key);
    if (ready != m_ready.end()) {
        m_lru.splice(m_lru.begin(), m_lru, m_lruIndex[key]);
        return true;
    }

    if (level > 0 && m_gpuOrtho) {
        request(level - 1, tx >> 1, ty >> 1);
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_failed.find(key) != m_failed.end() || m_pending.find(key) != m_pending.end() ||
        m_queue.size() >= kMaxQueuedTiles) {
        return false;
    }
    m_pending.insert(key);
    m_queue.push_back({key, level, tx, ty});
    ++m_stats.misses;
    m_condition.notify_one();
    return false;
}

bool TerrainMaterialTileStreamer::prefetch(int level, int tx, int ty) {
    if (!m_loaded || m_worldSize <= 0.0f || level < 0 || level >= 31) {
        return false;
    }
    const int count = 1 << level;
    if (tx < 0 || ty < 0 || tx >= count || ty >= count) {
        return false;
    }

    const uint64_t key = tileKey(level, tx, ty);
    if (m_ready.find(key) != m_ready.end()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_failed.find(key) != m_failed.end() || m_pending.find(key) != m_pending.end() ||
        m_queue.size() >= kMaxQueuedTiles || m_lru.size() + m_pending.size() >= m_capacity) {
        return false;
    }
    m_pending.insert(key);
    m_queue.push_back({key, level, tx, ty});
    ++m_stats.prefetched;
    m_condition.notify_one();
    return true;
}

uint32_t TerrainMaterialTileStreamer::freeSlots() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::size_t used = m_lru.size() + m_pending.size();
    return used >= m_capacity ? 0u : static_cast<uint32_t>(m_capacity - used);
}

void TerrainMaterialTileStreamer::makeBinding(int level, int tx, int ty, uint32_t layer,
                                               Binding& binding) const {
    const float tileWorld = m_worldSize / static_cast<float>(1 << level);
    const float borderWorld = tileWorld * (static_cast<float>(kTileBorder) / static_cast<float>(m_contentSize));
    binding.layer = static_cast<int>(layer * 2u);
    binding.originX = m_originX + static_cast<float>(tx) * tileWorld - borderWorld;
    binding.originZ = m_originZ + static_cast<float>(ty) * tileWorld - borderWorld;
    binding.size = tileWorld + 2.0f * borderWorld;
}

void TerrainMaterialTileStreamer::applyOrtho(int level, float worldX, float worldZ, uint8_t* color) const {
    if (m_ortho.mode == OrthoMode::Baked) {
        return;
    }
    const float amplitude = level >= 0 && static_cast<std::size_t>(level) < m_ortho.noiseAmp.size()
                                ? m_ortho.noiseAmp[static_cast<std::size_t>(level)] / 255.0f
                                : 0.0f;
    if (m_ortho.mode == OrthoMode::Detail && amplitude <= 0.0f) {
        return;
    }
    const int scale = 1 << std::min(std::max(level, 0), 20);
    const float frequency = static_cast<float>(scale) * 16.0f / m_worldSize;
    const float noise = valueNoise(worldX * frequency, worldZ * frequency);
    const float amount = m_ortho.strength * m_ortho.scale * amplitude * noise;
    if (m_ortho.mode == OrthoMode::Procedural) {
        color[0] = static_cast<uint8_t>(std::clamp(m_ortho.rootColor.x * 255.0f, 0.0f, 255.0f));
        color[1] = static_cast<uint8_t>(std::clamp(m_ortho.rootColor.y * 255.0f, 0.0f, 255.0f));
        color[2] = static_cast<uint8_t>(std::clamp(m_ortho.rootColor.z * 255.0f, 0.0f, 255.0f));
    }
    const float noiseValues[3] = {m_ortho.noiseColor.x, m_ortho.noiseColor.y, m_ortho.noiseColor.z};
    for (int channel = 0; channel < 3; ++channel) {
        const float base = static_cast<float>(color[channel]);
        const float delta = noiseValues[channel] * 255.0f * amount;
        color[channel] = static_cast<uint8_t>(std::clamp(base + delta, 0.0f, 255.0f));
    }
}

bool TerrainMaterialTileStreamer::renderGpuTile(const TileResult& result, uint32_t slot, uint32_t parentSlot,
                                                uint32_t viewOrdinal) {
    if (!m_gpuOrtho || m_resources == nullptr || result.level < 0 || result.level >= 31 ||
        slot >= m_capacity || !m_orthoScratchTexture.valid() || !bgfx::isValid(m_orthoScratchFramebuffer) ||
        viewOrdinal >= kOrthoViewCount) {
        return false;
    }
    if (result.level > 0 && parentSlot == ~0u) {
        return false;
    }

    const bgfx::ViewId view = static_cast<bgfx::ViewId>(kOrthoViewBase + viewOrdinal * 2u);
    bgfx::setViewFrameBuffer(view, m_orthoScratchFramebuffer);
    bgfx::setViewRect(view, 0, 0, static_cast<uint16_t>(m_textureSize), static_cast<uint16_t>(m_textureSize));
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    const gfx::Buffer* vertexBuffer = m_resources->buffer(m_orthoVertexBuffer);
    if (vertexBuffer == nullptr) {
        return false;
    }
    bgfx::setVertexBuffer(0, bgfx::VertexBufferHandle{vertexBuffer->nativeIndex()});

    const int count = 1 << result.level;
    const float tileWorld = m_worldSize / static_cast<float>(count);
    const float borderWorld = tileWorld * (static_cast<float>(kTileBorder) / static_cast<float>(m_contentSize));
    const float textureWorld = tileWorld + 2.0f * borderWorld;
    const float textureOriginX = m_originX + static_cast<float>(result.tx) * tileWorld - borderWorld;
    const float textureOriginZ = m_originZ + static_cast<float>(result.ty) * tileWorld - borderWorld;

    const float parentSize = static_cast<float>(m_textureSize);
    const float parentOffsetX = result.level > 0
                                    ? static_cast<float>(result.tx % 2) * (static_cast<float>(m_contentSize) * 0.5f)
                                    : 0.0f;
    const float parentOffsetZ = result.level > 0
                                    ? static_cast<float>(result.ty % 2) * (static_cast<float>(m_contentSize) * 0.5f)
                                    : 0.0f;
    const float amplitude = result.level >= 0 && static_cast<std::size_t>(result.level) < m_ortho.noiseAmp.size()
                                ? m_ortho.noiseAmp[static_cast<std::size_t>(result.level)] / 255.0f
                                : 0.0f;
    const bool hasParent = result.level > 0;
    const bool hasSource = result.level == 0 && m_ortho.mode == OrthoMode::Detail;
    const bool procedural = m_ortho.mode == OrthoMode::Procedural;

    const Vec4 transform{textureOriginX, textureOriginZ, textureWorld, m_worldSize};
    const Vec4 coarse{(parentOffsetX + 0.5f) / parentSize, (parentOffsetZ + 0.5f) / parentSize,
                      1.0f / parentSize, static_cast<float>(parentSlot * 2u)};
    const Vec4 info{static_cast<float>(result.level), m_ortho.strength * m_ortho.scale, amplitude,
                    static_cast<float>(m_textureSize)};
    const Vec4 flags{hasParent ? 1.0f : 0.0f, hasSource ? 1.0f : 0.0f, procedural ? 1.0f : 0.0f,
                     m_ortho.hsv ? 1.0f : 0.0f};
    const Vec4 color{m_ortho.noiseColor.x, m_ortho.noiseColor.y, m_ortho.noiseColor.z, 1.0f};
    const Vec4 root{m_ortho.rootColor.x, m_ortho.rootColor.y, m_ortho.rootColor.z, 1.0f};
    const OrthoNoiseSelection noise = selectOrthoNoise(result.level, result.tx, result.ty);
    const Vec4 noiseInfo{static_cast<float>(noise.uvX), static_cast<float>(noise.uvY),
                         static_cast<float>(noise.layer), 0.0f};

    bgfx::setUniform(m_resources->native(m_orthoTransformUniform), &transform, 1);
    bgfx::setUniform(m_resources->native(m_orthoCoarseUniform), &coarse, 1);
    bgfx::setUniform(m_resources->native(m_orthoInfoUniform), &info, 1);
    bgfx::setUniform(m_resources->native(m_orthoFlagsUniform), &flags, 1);
    bgfx::setUniform(m_resources->native(m_orthoColorUniform), &color, 1);
    bgfx::setUniform(m_resources->native(m_orthoRootUniform), &root, 1);
    bgfx::setUniform(m_resources->native(m_orthoNoiseUniform), &noiseInfo, 1);

    const bgfx::TextureHandle tileTexture = m_resources->native(m_gpu.texture());
    const bgfx::TextureHandle sourceTexture =
        m_orthoSourceTexture.valid() ? m_resources->native(m_orthoSourceTexture) : tileTexture;
    bgfx::setTexture(0, m_resources->native(m_orthoParentSampler), tileTexture, BGFX_SAMPLER_UVW_CLAMP);
    bgfx::setTexture(1, m_resources->native(m_orthoSourceSampler), sourceTexture, BGFX_SAMPLER_UVW_CLAMP);
    bgfx::setTexture(2, m_resources->native(m_orthoNoiseSampler), m_resources->native(m_orthoNoiseTexture),
                     BGFX_SAMPLER_UVW_CLAMP);
    const gfx::Program* program = m_resources->program(m_orthoProgram);
    if (program == nullptr || !program->valid()) {
        return false;
    }
    bgfx::submit(view, program->native());

    const bgfx::ViewId blitView = static_cast<bgfx::ViewId>(view + 1u);
    const bgfx::TextureHandle scratchTexture = m_resources->native(m_orthoScratchTexture);
    uint32_t width = m_textureSize;
    uint32_t height = m_textureSize;
    for (uint8_t mip = 0; mip < m_gpu.mipCount(); ++mip) {
        bgfx::TextureRegion destination;
        destination.handle = tileTexture;
        destination.mip = mip;
        destination.z = static_cast<uint16_t>(slot * 2u);
        destination.width = static_cast<uint16_t>(width);
        destination.height = static_cast<uint16_t>(height);
        bgfx::TextureRegion source;
        source.handle = scratchTexture;
        source.mip = mip;
        source.width = static_cast<uint16_t>(width);
        source.height = static_cast<uint16_t>(height);
        bgfx::blit(blitView, destination, source);
        width = std::max(1u, width / 2u);
        height = std::max(1u, height / 2u);
    }
    bgfx::touch(blitView);
    return true;
}

bool TerrainMaterialTileStreamer::bind(int level, int tx, int ty, Binding& binding) {
    if (!m_loaded || level < 0 || level >= 31) {
        return false;
    }
    const int count = 1 << level;
    if (tx < 0 || ty < 0 || tx >= count || ty >= count) {
        return false;
    }

    const uint64_t key = tileKey(level, tx, ty);
    const auto found = m_ready.find(key);
    if (found != m_ready.end()) {
        m_lru.splice(m_lru.begin(), m_lru, m_lruIndex[key]);
        makeBinding(level, tx, ty, found->second, binding);
        ++m_stats.hits;
        return true;
    }

    for (int parentLevel = level - 1; parentLevel >= 0; --parentLevel) {
        const int shift = level - parentLevel;
        const int parentTx = tx >> shift;
        const int parentTy = ty >> shift;
        const auto parent = m_ready.find(tileKey(parentLevel, parentTx, parentTy));
        if (parent != m_ready.end()) {
            m_lru.splice(m_lru.begin(), m_lru, m_lruIndex[parent->first]);
            makeBinding(parentLevel, parentTx, parentTy, parent->second, binding);
            ++m_stats.hits;
            ++m_stats.fallbacks;
            return true;
        }
    }
    return false;
}

void TerrainMaterialTileStreamer::poll() {
    std::vector<TileResult> completed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        completed.swap(m_completed);
    }

    std::sort(completed.begin(), completed.end(), [](const TileResult& lhs, const TileResult& rhs) {
        return lhs.level != rhs.level ? lhs.level < rhs.level : lhs.key < rhs.key;
    });

    uint32_t viewOrdinal = 0;
    for (TileResult& result : completed) {
        int slotResult = -1;
        bool gpuAttempted = false;
        if (result.ok) {
            slotResult = assignGpuSlot(result, viewOrdinal, gpuAttempted);
            if (gpuAttempted) {
                ++viewOrdinal;
            }
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.erase(result.key);
        if (result.ok && slotResult >= 0) {
            ++m_stats.produced;
            m_stats.uploads += 2;
            if (slotResult > 0) {
                ++m_stats.evictions;
            }
        } else {
            m_failed.insert(result.key);
            ++m_stats.failed;
        }
    }
}

TerrainMaterialTileStreamer::Stats TerrainMaterialTileStreamer::stats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    Stats snapshot = m_stats;
    snapshot.pending = static_cast<uint32_t>(m_pending.size());
    snapshot.residentTiles = static_cast<uint32_t>(m_lru.size());
    snapshot.gpuLayers = static_cast<uint32_t>(m_lru.size() * 2u);
    snapshot.gpuBytes = m_gpu.gpuBytes();
    return snapshot;
}

void TerrainMaterialTileStreamer::run() {
    for (;;) {
        TileRequest request;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, [this]() { return m_stop || !m_queue.empty(); });
            if (m_stop && m_queue.empty()) {
                break;
            }
            request = m_queue.front();
            m_queue.pop_front();
        }

        TileResult result;
        result.key = request.key;
        result.level = request.level;
        result.tx = request.tx;
        result.ty = request.ty;
        result.ok = produceTile(request, result);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_completed.push_back(std::move(result));
        }
    }
}

bool TerrainMaterialTileStreamer::produceTile(const TileRequest& request, TileResult& result) const {
    if (request.level < 0 || request.level >= 31) {
        return false;
    }
    const int count = 1 << request.level;
    if (request.tx < 0 || request.ty < 0 || request.tx >= count || request.ty >= count) {
        return false;
    }

    const float size = m_worldSize / static_cast<float>(count);
    const float ox = m_originX + static_cast<float>(request.tx) * size;
    const float oz = m_originZ + static_cast<float>(request.ty) * size;
    const float borderWorld = size * (static_cast<float>(kTileBorder) / static_cast<float>(m_contentSize));
    const float textureWorld = size + 2.0f * borderWorld;
    const float textureOriginX = ox - borderWorld;
    const float textureOriginZ = oz - borderWorld;
    const float half = m_worldSize * 0.5f;
    const uint32_t layerPixels = m_textureSize * m_textureSize;
    const uint32_t layerBytes = layerPixels * kChannels;
    result.pixels.resize(static_cast<std::size_t>(layerBytes) * 2u);

    for (uint32_t py = 0; py < m_textureSize; ++py) {
        const float localV = (static_cast<float>(py) + 0.5f) / static_cast<float>(m_textureSize);
        const float worldZ = textureOriginZ + localV * textureWorld;
        const float globalV = (worldZ + half) / m_worldSize;
        for (uint32_t px = 0; px < m_textureSize; ++px) {
            const float localU = (static_cast<float>(px) + 0.5f) / static_cast<float>(m_textureSize);
            const float worldX = textureOriginX + localU * textureWorld;
            const float globalU = (worldX + half) / m_worldSize;
            const std::size_t index = (static_cast<std::size_t>(py) * m_textureSize + px) * kChannels;

            uint8_t albedo[4] = {255, 255, 255, 255};
            sampleImage(*m_albedo, globalU, globalV, albedo);
            applyOrtho(request.level, worldX, worldZ, albedo);
            albedo[3] = 255;
            std::memcpy(result.pixels.data() + index, albedo, kChannels);

            uint8_t normal[4] = {128, 128, 0, 255};
            sampleImage(*m_normal, globalU, globalV, normal);
            uint8_t* normalOut = result.pixels.data() + layerBytes + index;
            normalOut[0] = linearToSrgb8(normal[0]);
            normalOut[1] = linearToSrgb8(normal[1]);
            normalOut[2] = 255;
            normalOut[3] = 255;
        }
    }
    return true;
}

void TerrainMaterialTileStreamer::sampleImage(const asset::ImageAsset& image, float u, float v, uint8_t* out) {
    if (image.width == 0 || image.height == 0 || image.pixels.empty()) {
        out[0] = 255;
        out[1] = 255;
        out[2] = 255;
        out[3] = 255;
        return;
    }

    const float x = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.width - 1);
    const float y = std::clamp(v, 0.0f, 1.0f) * static_cast<float>(image.height - 1);
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(x0 + 1u, image.width - 1u);
    const uint32_t y1 = std::min(y0 + 1u, image.height - 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const std::size_t stride = static_cast<std::size_t>(image.width) * kChannels;
    const uint8_t* row0 = image.pixels.data() + static_cast<std::size_t>(y0) * stride + x0 * kChannels;
    const uint8_t* row1 = image.pixels.data() + static_cast<std::size_t>(y1) * stride + x0 * kChannels;
    const uint8_t* row0n = image.pixels.data() + static_cast<std::size_t>(y0) * stride + x1 * kChannels;
    const uint8_t* row1n = image.pixels.data() + static_cast<std::size_t>(y1) * stride + x1 * kChannels;
    for (uint32_t channel = 0; channel < kChannels; ++channel) {
        const float top = static_cast<float>(row0[channel]) +
                          (static_cast<float>(row0n[channel]) - static_cast<float>(row0[channel])) * tx;
        const float bottom = static_cast<float>(row1[channel]) +
                             (static_cast<float>(row1n[channel]) - static_cast<float>(row1[channel])) * tx;
        const float value = top + (bottom - top) * ty;
        out[channel] = static_cast<uint8_t>(std::clamp(std::lround(value), 0l, 255l));
    }
}

bool TerrainMaterialTileStreamer::uploadMipChain(uint32_t layer, const uint8_t* base) {
    uint32_t width = m_textureSize;
    uint32_t height = m_textureSize;
    std::vector<uint8_t> previous(base, base + static_cast<std::size_t>(width) * height * kChannels);
    if (!m_gpu.upload(layer, previous.data(), static_cast<uint32_t>(previous.size()), width * kChannels)) {
        return false;
    }
    for (uint8_t mip = 1; mip < m_gpu.mipCount(); ++mip) {
        const uint32_t nextWidth = std::max(1u, width / 2u);
        const uint32_t nextHeight = std::max(1u, height / 2u);
        std::vector<uint8_t> next(static_cast<std::size_t>(nextWidth) * nextHeight * kChannels);
        for (uint32_t y = 0; y < nextHeight; ++y) {
            for (uint32_t x = 0; x < nextWidth; ++x) {
                for (uint32_t channel = 0; channel < kChannels; ++channel) {
                    uint32_t sum = 0;
                    uint32_t count = 0;
                    for (uint32_t dy = 0; dy < 2; ++dy) {
                        for (uint32_t dx = 0; dx < 2; ++dx) {
                            const uint32_t sourceX = std::min(x * 2u + dx, width - 1u);
                            const uint32_t sourceY = std::min(y * 2u + dy, height - 1u);
                            sum += previous[(static_cast<std::size_t>(sourceY) * width + sourceX) * kChannels +
                                            channel];
                            ++count;
                        }
                    }
                    next[(static_cast<std::size_t>(y) * nextWidth + x) * kChannels + channel] =
                        static_cast<uint8_t>((sum + count / 2u) / count);
                }
            }
        }
        if (!m_gpu.upload(layer, mip, next.data(), static_cast<uint32_t>(next.size()),
                          nextWidth * kChannels, nextWidth, nextHeight)) {
            return false;
        }
        previous.swap(next);
        width = nextWidth;
        height = nextHeight;
    }
    return true;
}

int TerrainMaterialTileStreamer::assignGpuSlot(const TileResult& result, uint32_t viewOrdinal,
                                               bool& gpuAttempted) {
    const uint64_t key = result.key;
    gpuAttempted = false;
    if (!m_gpu.valid() || result.pixels.size() < static_cast<std::size_t>(m_textureSize) * m_textureSize * 8u) {
        return -1;
    }

    uint64_t parentKey = 0;
    uint32_t parentSlot = ~0u;
    if (result.level > 0) {
        parentKey = tileKey(result.level - 1, result.tx >> 1, result.ty >> 1);
        const auto parent = m_ready.find(parentKey);
        if (parent != m_ready.end()) {
            parentSlot = parent->second;
        }
    }

    uint32_t slot = 0;
    bool foundFree = false;
    bool evicted = false;
    if (m_freeSlots > 0) {
        for (uint32_t candidate = 0; candidate < m_capacity; ++candidate) {
            if (m_slotUsed[candidate] == 0) {
                slot = candidate;
                foundFree = true;
                break;
            }
        }
    }
    if (!foundFree) {
        if (m_lru.empty()) {
            return -1;
        }
        auto victim = m_lru.end();
        for (auto candidate = m_lru.end(); candidate != m_lru.begin();) {
            --candidate;
            if (result.level > 0 && candidate->key == parentKey && m_lru.size() > 1) {
                continue;
            }
            victim = candidate;
            break;
        }
        if (victim == m_lru.end()) {
            return -1;
        }
        slot = victim->slot;
        m_lruIndex.erase(victim->key);
        m_ready.erase(victim->key);
        m_lru.erase(victim);
        evicted = true;
    }

    bool albedoUploaded = false;
    const bool useGpu = m_gpuOrtho && (result.level == 0 || parentSlot != ~0u) && viewOrdinal < kOrthoViewCount;
    if (useGpu) {
        gpuAttempted = true;
        albedoUploaded = renderGpuTile(result, slot, parentSlot, viewOrdinal);
        if (albedoUploaded) {
            ++m_stats.gpuProduced;
        } else {
            ++m_stats.gpuFailed;
        }
    }

    const uint32_t layerBytes = m_textureSize * m_textureSize * kChannels;
    if (!uploadMipChain(slot * 2u + 1u, result.pixels.data() + layerBytes) ||
        (!albedoUploaded && !uploadMipChain(slot * 2u, result.pixels.data()))) {
        if (evicted) {
            m_slotUsed[slot] = 0;
            ++m_freeSlots;
        }
        return -1;
    }
    if (foundFree) {
        m_slotUsed[slot] = 1;
        --m_freeSlots;
    }
    m_ready[key] = slot;
    m_lru.push_front({key, slot});
    m_lruIndex[key] = m_lru.begin();
    m_tileMapDirty = m_tileMapEnabled;
    return evicted ? 1 : 0;
}

} // namespace hbg::render

