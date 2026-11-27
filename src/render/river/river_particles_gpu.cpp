#include "render/river/river_particles_gpu.h"

#include "core/log.h"

#include <algorithm>
#include <string>

namespace hbg::render {
namespace {

std::string shaderBackend() {
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return "dxbc";
    case bgfx::RendererType::Vulkan:
        return "spirv";
    default:
        return {};
    }
}

constexpr uint32_t kTextureSamplerFlags =
    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
constexpr uint32_t kParameterTextureWidth = 2;
constexpr uint32_t kParameterFloatsPerParticle = 8;
constexpr uint32_t kSpriteTableLayers = 4;
constexpr uint32_t kSpriteTableFloatsPerCell = 16;

} // namespace

bool RiverParticleGpu::init(gfx::ResourceManager& resources, const RiverParticleSystem& river,
                            gfx::TextureHandle waveBaseTexture, const std::filesystem::path& shaderRoot) {
    m_resources = &resources;
    m_valid = false;
    if (!river.valid() || !waveBaseTexture.valid()) {
        return false;
    }
    m_capacity = std::max(river.gpuParticleCapacity(), 1u);
    if (m_capacity > UINT16_MAX) {
        HBG_LOGW("river particles gpu: capacity {} exceeds texture height limit", m_capacity);
        return false;
    }
    m_waveBaseTexture = waveBaseTexture;
    if (!createProgram(resources, shaderRoot) || !createGridBuffer(resources, 6) ||
        !createParameterTexture(resources) || !createUniforms(resources)) {
        shutdown(resources);
        return false;
    }
    m_valid = true;
    HBG_LOGI("river particles gpu: initialized capacity={} format=RGBA32F grid-sprite backend={}", m_capacity,
             shaderBackend());
    return true;
}

bool RiverParticleGpu::createProgram(gfx::ResourceManager& resources, const std::filesystem::path& shaderRoot) {
    if (shaderRoot.empty()) {
        return false;
    }
    const std::string backend = shaderBackend();
    if (backend.empty()) {
        return false;
    }
    const std::filesystem::path vertexPath = shaderRoot / "build" / backend / "river_particles.vs.bin";
    const std::filesystem::path fragmentPath = shaderRoot / "build" / backend / "river_particles.fs.bin";
    m_program = resources.createProgramFromFiles(vertexPath.string(), fragmentPath.string(), "river_particles");
    if (!m_program.valid()) {
        HBG_LOGE("river particles gpu: failed to load program '{}' + '{}'", vertexPath.string(), fragmentPath.string());
        return false;
    }
    return true;
}

bool RiverParticleGpu::createGridBuffer(gfx::ResourceManager& resources, uint32_t vertexCapacity) {
    if (m_gridBuffer.valid()) {
        resources.destroyBuffer(m_gridBuffer);
        m_gridBuffer = {};
    }
    gfx::VertexLayout layout;
    layout.position().end();
    gfx::BufferDesc desc;
    desc.type = gfx::BufferType::DynamicVertex;
    desc.count = std::max(vertexCapacity, 6u);
    desc.layout = &layout;
    desc.name = "river_particle_grid_spans";
    m_gridBuffer = resources.createBuffer(desc);
    m_gridVertexCapacity = m_gridBuffer.valid() ? desc.count : 0u;
    return m_gridBuffer.valid();
}

bool RiverParticleGpu::ensureGridBuffer(gfx::ResourceManager& resources, uint32_t vertexCount) {
    if (m_gridBuffer.valid() && m_gridVertexCapacity >= vertexCount) {
        return true;
    }
    return createGridBuffer(resources, vertexCount);
}

bool RiverParticleGpu::createParameterTexture(gfx::ResourceManager& resources) {
    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(kParameterTextureWidth);
    desc.height = static_cast<uint16_t>(m_capacity);
    desc.format = gfx::TextureFormat::RGBA32F;
    desc.flags = kTextureSamplerFlags;
    desc.name = "river_particle_params";
    m_parameterTexture = resources.createTexture(desc);
    if (!m_parameterTexture.valid()) {
        HBG_LOGE("river particles gpu: failed to create parameter texture {}x{}", kParameterTextureWidth, m_capacity);
        return false;
    }
    return true;
}

bool RiverParticleGpu::ensureSpriteTableTexture(gfx::ResourceManager& resources, uint32_t gridWidth,
                                                uint32_t gridHeight) {
    if (m_spriteTableTexture.valid() && m_spriteGridWidth == gridWidth && m_spriteGridHeight == gridHeight) {
        return true;
    }
    destroySpriteTableTexture(resources);
    if (gridWidth == 0 || gridHeight == 0 || gridWidth * kSpriteTableLayers > UINT16_MAX ||
        gridHeight > UINT16_MAX) {
        return false;
    }
    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(gridWidth * kSpriteTableLayers);
    desc.height = static_cast<uint16_t>(gridHeight);
    desc.format = gfx::TextureFormat::RGBA32F;
    desc.flags = kTextureSamplerFlags;
    desc.name = "river_particle_sprite_table";
    m_spriteTableTexture = resources.createTexture(desc);
    if (!m_spriteTableTexture.valid()) {
        HBG_LOGE("river particles gpu: failed to create sprite table {}x{}", desc.width, desc.height);
        return false;
    }
    m_spriteGridWidth = gridWidth;
    m_spriteGridHeight = gridHeight;
    HBG_LOGI("river particles gpu: sprite table={}x{} cells={}x{}", desc.width, desc.height, gridWidth, gridHeight);
    return true;
}

bool RiverParticleGpu::createNormalTarget(gfx::ResourceManager& resources, uint16_t width, uint16_t height) {
    if (m_normalTexture.valid()) {
        resources.destroyTexture(m_normalTexture);
        m_normalTexture = {};
    }
    gfx::TextureDesc normalDesc;
    normalDesc.width = width;
    normalDesc.height = height;
    normalDesc.format = gfx::TextureFormat::RGBA32F;
    normalDesc.renderTarget = true;
    normalDesc.flags = BGFX_TEXTURE_NONE;
    normalDesc.name = "river_particle_normal";
    m_normalTexture = resources.createTexture(normalDesc);
    m_targetWidth = m_normalTexture.valid() ? width : 0u;
    m_targetHeight = m_normalTexture.valid() ? height : 0u;
    if (!m_normalTexture.valid()) {
        HBG_LOGE("river particles gpu: failed to create {}x{} residual target", width, height);
        return false;
    }
    HBG_LOGI("river particles gpu: target=viewport {}x{} format=RGBA32F linear-repeat", width, height);
    return true;
}

bool RiverParticleGpu::ensureTarget(gfx::ResourceManager& resources, uint16_t width, uint16_t height) {
    if (m_normalTexture.valid() && m_targetWidth == width && m_targetHeight == height) {
        return true;
    }
    return createNormalTarget(resources, width, height);
}

bool RiverParticleGpu::createUniforms(gfx::ResourceManager& resources) {
    m_invViewProjUniform = resources.createUniform({"u_riverParticleInvViewProj", gfx::UniformType::Mat4, 1});
    m_cameraUniform = resources.createUniform({"u_riverParticleCamera", gfx::UniformType::Vec4, 1});
    m_paramsUniform = resources.createUniform({"u_riverParticleParams", gfx::UniformType::Vec4, 1});
    m_gridUniform = resources.createUniform({"u_riverParticleGrid", gfx::UniformType::Vec4, 1});
    m_waveSampler = resources.createUniform({"s_riverWaveBase", gfx::UniformType::Sampler, 1});
    m_parameterSampler = resources.createUniform({"s_riverParticleParams", gfx::UniformType::Sampler, 1});
    m_spriteTableSampler = resources.createUniform({"s_riverParticleSpriteTable", gfx::UniformType::Sampler, 1});
    m_depthSampler = resources.createUniform({"s_riverParticleDepth", gfx::UniformType::Sampler, 1});
    return m_invViewProjUniform.valid() && m_cameraUniform.valid() && m_paramsUniform.valid() &&
           m_gridUniform.valid() && m_waveSampler.valid() && m_parameterSampler.valid() &&
           m_spriteTableSampler.valid() && m_depthSampler.valid();
}

bool RiverParticleGpu::uploadTables(gfx::ResourceManager& resources, const RiverParticleSystem& river) {
    const auto& params = river.gpuParticleParams();
    const auto& spriteTable = river.gpuSpriteTable();
    const uint32_t gridWidth = river.gpuGridWidth();
    const uint32_t gridHeight = river.gpuGridHeight();
    if (gridWidth == 0 || gridHeight == 0 ||
        params.size() < static_cast<std::size_t>(m_capacity) * kParameterFloatsPerParticle ||
        spriteTable.size() < static_cast<std::size_t>(gridWidth) * gridHeight * kSpriteTableFloatsPerCell) {
        return false;
    }
    const uint16_t parameterPitch =
        static_cast<uint16_t>(kParameterTextureWidth * 4u * sizeof(float));
    const uint32_t parameterRows = std::min(river.gpuParticleCount(), m_capacity);
    if (parameterRows > 0) {
        const uint32_t parameterSize = static_cast<uint32_t>(parameterPitch) * parameterRows;
        if (!resources.updateTexture(m_parameterTexture, params.data(), parameterSize, kParameterTextureWidth,
                                     static_cast<uint16_t>(parameterRows), parameterPitch)) {
            HBG_LOGW("river particles gpu: failed to upload parameter table");
            return false;
        }
    }
    const uint32_t spriteWidth = gridWidth * kSpriteTableLayers;
    if (spriteWidth > UINT16_MAX) {
        return false;
    }
    const uint32_t spritePitch = spriteWidth * 4u * sizeof(float);
    const uint32_t spriteSize = spritePitch * gridHeight;
    if (spritePitch > UINT16_MAX ||
        !resources.updateTexture(m_spriteTableTexture, spriteTable.data(), spriteSize,
                                 static_cast<uint16_t>(spriteWidth), static_cast<uint16_t>(gridHeight),
                                 static_cast<uint16_t>(spritePitch))) {
        HBG_LOGW("river particles gpu: failed to upload sprite table");
        return false;
    }
    return true;
}

bool RiverParticleGpu::update(gfx::ResourceManager& resources, const RiverParticleSystem& river,
                              const Mat4& invViewProj, const Vec3& cameraPosition, uint16_t width,
                              uint16_t height) {
    if (!m_valid || width == 0 || height == 0 || !m_program.valid()) {
        return false;
    }
    m_ready = false;
    const uint32_t gridWidth = river.gpuGridWidth();
    const uint32_t gridHeight = river.gpuGridHeight();
    if (!ensureTarget(resources, width, height) || !ensureSpriteTableTexture(resources, gridWidth, gridHeight) ||
        !uploadTables(resources, river)) {
        return false;
    }

    const auto& spans = river.gpuGridSpans();
    m_gridSpanCount = static_cast<uint32_t>(spans.size());
    m_vertexCount = 0;
    if (!spans.empty()) {
        const uint32_t vertexCount = m_gridSpanCount * 6u;
        if (!ensureGridBuffer(resources, vertexCount)) {
            return false;
        }
        std::vector<float> vertices;
        vertices.reserve(static_cast<std::size_t>(vertexCount) * 3u);
        const float scaleX = 2.0f * river.gpuGridCellWidth() / static_cast<float>(width);
        const float scaleY = 2.0f * river.gpuGridCellHeight() / static_cast<float>(height);
        for (const RiverGridSpan& span : spans) {
            const float left = static_cast<float>(span.minX) * scaleX - 1.0f;
            const float right = static_cast<float>(span.maxX + 1u) * scaleX - 1.0f;
            const float top = 1.0f - static_cast<float>(span.row) * scaleY;
            const float bottom = 1.0f - static_cast<float>(span.row + 1u) * scaleY;
            vertices.push_back(left);
            vertices.push_back(bottom);
            vertices.push_back(0.0f);
            vertices.push_back(right);
            vertices.push_back(bottom);
            vertices.push_back(0.0f);
            vertices.push_back(left);
            vertices.push_back(top);
            vertices.push_back(0.0f);
            vertices.push_back(left);
            vertices.push_back(top);
            vertices.push_back(0.0f);
            vertices.push_back(right);
            vertices.push_back(bottom);
            vertices.push_back(0.0f);
            vertices.push_back(right);
            vertices.push_back(top);
            vertices.push_back(0.0f);
        }
        if (!resources.updateBuffer(m_gridBuffer, vertices.data(),
                                    static_cast<uint32_t>(vertices.size() * sizeof(float)))) {
            HBG_LOGW("river particles gpu: failed to upload grid spans");
            return false;
        }
        m_vertexCount = vertexCount;
    }

    m_invViewProj = invViewProj;
    m_camera = Vec4(cameraPosition.x, cameraPosition.y, cameraPosition.z, river.baseHeight());
    m_params0 = Vec4(static_cast<float>(width), static_cast<float>(height), river.waveLength(), 0.0f);
    m_gridParams = Vec4(static_cast<float>(gridWidth), static_cast<float>(gridHeight), 0.0f,
                        static_cast<float>(m_capacity));
    if (!m_gridLogged) {
        HBG_LOGI("river particles gpu: grid={}x{} cell={}x{} particles={} param_rows={} spans={} vertices={} "
                 "capacity={}",
                 gridWidth, gridHeight, river.gpuGridCellWidth(), river.gpuGridCellHeight(),
                 river.gpuParticleCount(), std::min(river.gpuParticleCount(), m_capacity), m_gridSpanCount,
                 m_vertexCount, m_capacity);
        m_gridLogged = true;
    }
    m_ready = true;
    return true;
}

void RiverParticleGpu::submit(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                              gfx::TextureHandle depthTexture) {
    if (!m_valid || !m_ready || !m_program.valid() || !depthTexture.valid() || !m_waveBaseTexture.valid() ||
        !m_normalTexture.valid() || !m_parameterTexture.valid() || !m_spriteTableTexture.valid() ||
        !m_gridBuffer.valid()) {
        return;
    }
    encoder.setViewRect(view, 0, 0, static_cast<uint16_t>(m_targetWidth), static_cast<uint16_t>(m_targetHeight));
    encoder.setViewFrameBuffer(view, frameBuffer);
    encoder.setViewClear(view, BGFX_CLEAR_COLOR, 0x00000000);
    encoder.touch(view);
    if (m_vertexCount == 0) {
        return;
    }
    encoder.setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    encoder.setTexture(0, m_waveSampler, m_waveBaseTexture);
    encoder.setTexture(1, m_parameterSampler, m_parameterTexture, kTextureSamplerFlags);
    encoder.setTexture(2, m_spriteTableSampler, m_spriteTableTexture, kTextureSamplerFlags);
    encoder.setTexture(3, m_depthSampler, depthTexture,
                       BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_POINT);
    encoder.setUniform(m_invViewProjUniform, m_invViewProj.values.data(), 1);
    encoder.setUniform(m_cameraUniform, &m_camera, 1);
    encoder.setUniform(m_paramsUniform, &m_params0, 1);
    encoder.setUniform(m_gridUniform, &m_gridParams, 1);
    encoder.setVertexBuffer(0, m_gridBuffer, 0, m_vertexCount);
    encoder.submit(view, m_program);
}

void RiverParticleGpu::destroySpriteTableTexture(gfx::ResourceManager& resources) {
    if (m_spriteTableTexture.valid()) {
        resources.destroyTexture(m_spriteTableTexture);
        m_spriteTableTexture = {};
    }
    m_spriteGridWidth = 0;
    m_spriteGridHeight = 0;
}

void RiverParticleGpu::shutdown(gfx::ResourceManager& resources) {
    const auto destroyTexture = [&](gfx::TextureHandle& texture) {
        if (texture.valid()) {
            resources.destroyTexture(texture);
            texture = {};
        }
    };
    const auto destroyUniform = [&](gfx::UniformHandle& uniform) {
        if (uniform.valid()) {
            resources.destroyUniform(uniform);
            uniform = {};
        }
    };
    if (m_gridBuffer.valid()) {
        resources.destroyBuffer(m_gridBuffer);
        m_gridBuffer = {};
    }
    destroyTexture(m_normalTexture);
    destroySpriteTableTexture(resources);
    destroyTexture(m_parameterTexture);
    m_waveBaseTexture = {};
    destroyUniform(m_invViewProjUniform);
    destroyUniform(m_cameraUniform);
    destroyUniform(m_paramsUniform);
    destroyUniform(m_gridUniform);
    destroyUniform(m_waveSampler);
    destroyUniform(m_parameterSampler);
    destroyUniform(m_spriteTableSampler);
    destroyUniform(m_depthSampler);
    if (m_program.valid()) {
        resources.destroyProgram(m_program);
        m_program = {};
    }
    m_ready = false;
    m_gridLogged = false;
    m_targetWidth = 0;
    m_targetHeight = 0;
    m_capacity = 0;
    m_vertexCount = 0;
    m_gridVertexCapacity = 0;
    m_gridSpanCount = 0;
    m_spriteGridWidth = 0;
    m_spriteGridHeight = 0;
    m_valid = false;
}

} // namespace hbg::render
