#include "render/water/ocean_fft_gpu.h"

#include "core/log.h"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace hbg::render {
namespace {

constexpr uint32_t kWaterFftResolution = 128;
constexpr uint32_t kWaterFftWorkLayers = 5;
constexpr uint32_t kWaterFftLayers = 5;
constexpr uint32_t kWaterFftMips = 8;
constexpr bgfx::ViewId kWaterFftView = 1;

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

} // namespace

bool OceanFftGpu::init(gfx::ResourceManager& resources, const OceanFFT& ocean, const std::filesystem::path& shaderRoot) {
    m_resources = &resources;
    m_valid = false;
    m_resolution = ocean.resolution();
    m_mipCount = ocean.mipLevelCount();
    if (!ocean.valid() || m_resolution != kWaterFftResolution || m_mipCount != kWaterFftMips) {
        HBG_LOGI("ocean fft gpu: unsupported FFT settings (resolution={}, mips={})", m_resolution, m_mipCount);
        return false;
    }
    if (shaderRoot.empty()) {
        HBG_LOGI("ocean fft gpu: shader root is empty; using CPU FFT");
        return false;
    }

    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr || (caps->supported & BGFX_CAPS_COMPUTE) == 0 ||
        (caps->supported & BGFX_CAPS_IMAGE_RW) == 0) {
        HBG_LOGW("ocean fft gpu: compute/imageRW unavailable; using CPU FFT");
        return false;
    }

    if (!createPrograms(resources, shaderRoot) || !createTextures(resources, ocean) ||
        !createUniforms(resources) || !uploadSpectrum(resources, ocean)) {
        HBG_LOGW("ocean fft gpu: initialization failed; using CPU FFT");
        shutdown(resources);
        return false;
    }

    m_valid = true;
    HBG_LOGI("ocean fft gpu: ready resolution={} layers={} mips={} spectrum=2xRGBA16F work=5xRGBA16F backend={}",
             m_resolution, kWaterFftLayers, m_mipCount, shaderBackend());
    return true;
}

bool OceanFftGpu::createPrograms(gfx::ResourceManager& resources, const std::filesystem::path& shaderRoot) {
    const std::string backend = shaderBackend();
    if (backend.empty()) {
        return false;
    }
    const std::filesystem::path directory = shaderRoot / "build" / backend;
    const std::array<const char*, static_cast<std::size_t>(Program::Count)> files = {
        "water_fft_prepare.cs.bin", "water_fft_horizontal.cs.bin", "water_fft_vertical.cs.bin",
        "water_fft_finalize.cs.bin", "water_fft_mip.cs.bin",       "water_fft_displace.cs.bin",
    };
    for (uint32_t index = 0; index < static_cast<uint32_t>(Program::Count); ++index) {
        const std::filesystem::path path = directory / files[index];
        m_programs[index] = resources.createComputeProgramFromFiles(path.string(), files[index]);
        if (!m_programs[index].valid()) {
            HBG_LOGE("ocean fft gpu: failed to load compute program '{}'", path.string());
            return false;
        }
    }
    return true;
}

bool OceanFftGpu::createTextures(gfx::ResourceManager& resources, const OceanFFT& ocean) {
    (void)ocean;
    gfx::TextureDesc spectrumDesc;
    spectrumDesc.width = static_cast<uint16_t>(m_resolution);
    spectrumDesc.height = static_cast<uint16_t>(m_resolution);
    spectrumDesc.layers = 2;
    spectrumDesc.format = gfx::TextureFormat::RGBA16F;
    spectrumDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    spectrumDesc.name = "ocean_fft_gpu.spectrum";
    m_spectrumTexture = resources.createTexture(spectrumDesc);

    const auto createWorkTexture = [&](const char* name) {
        gfx::TextureDesc desc;
        desc.width = static_cast<uint16_t>(m_resolution);
        desc.height = static_cast<uint16_t>(m_resolution);
        desc.layers = static_cast<uint16_t>(kWaterFftWorkLayers);
        desc.format = gfx::TextureFormat::RGBA16F;
        desc.computeWrite = true;
        desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        desc.name = name;
        return resources.createTexture(desc);
    };
    m_pingTexture = createWorkTexture("ocean_fft_gpu.ping");
    m_pongTexture = createWorkTexture("ocean_fft_gpu.pong");

    gfx::TextureDesc sourceDesc;
    sourceDesc.width = static_cast<uint16_t>(m_resolution);
    sourceDesc.height = static_cast<uint16_t>(m_resolution);
    sourceDesc.layers = static_cast<uint16_t>(kWaterFftLayers);
    sourceDesc.format = gfx::TextureFormat::RGBA16F;
    sourceDesc.computeWrite = true;
    sourceDesc.name = "ocean_fft_gpu.source";
    m_sourceTexture = resources.createTexture(sourceDesc);

    gfx::TextureDesc finalDesc;
    finalDesc.width = static_cast<uint16_t>(m_resolution);
    finalDesc.height = static_cast<uint16_t>(m_resolution);
    finalDesc.layers = static_cast<uint16_t>(kWaterFftLayers);
    finalDesc.mipCount = static_cast<uint8_t>(m_mipCount);
    finalDesc.format = gfx::TextureFormat::RGBA16F;
    finalDesc.computeWrite = true;
    finalDesc.flags = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
    finalDesc.name = "ocean_fft_gpu.final";
    m_finalTexture = resources.createTexture(finalDesc);

    return m_spectrumTexture.valid() && m_pingTexture.valid() && m_pongTexture.valid() &&
           m_sourceTexture.valid() && m_finalTexture.valid();
}

bool OceanFftGpu::createUniforms(gfx::ResourceManager& resources) {
    m_timeUniform = resources.createUniform({"u_wfTime", gfx::UniformType::Vec4, 1});
    m_gridSizesUniform = resources.createUniform({"u_wfGridSizes", gfx::UniformType::Vec4, 1});
    m_mipUniform = resources.createUniform({"u_wfMip", gfx::UniformType::Vec4, 1});
    m_invViewProjUniform = resources.createUniform({"u_wfInvViewProj", gfx::UniformType::Mat4, 1});
    m_cameraUniform = resources.createUniform({"u_wfCamera", gfx::UniformType::Vec4, 1});
    m_gridUniform = resources.createUniform({"u_wfGrid", gfx::UniformType::Vec4, 1});
    m_displaceUniform = resources.createUniform({"u_wfDisplace", gfx::UniformType::Vec4, 1});
    m_uvUniform = resources.createUniform({"u_wfUv", gfx::UniformType::Vec4, 1});
    m_sphereUniform = resources.createUniform({"u_wfSphere", gfx::UniformType::Vec4, 1});
    m_inputSampler = resources.createUniform({"s_waterFftInput", gfx::UniformType::Sampler, 1});
    m_spectrumSampler = resources.createUniform({"s_waterFftSpectrum", gfx::UniformType::Sampler, 1});
    m_finalSampler = resources.createUniform({"s_waterFftFinal", gfx::UniformType::Sampler, 1});
    return m_timeUniform.valid() && m_gridSizesUniform.valid() && m_mipUniform.valid() &&
           m_invViewProjUniform.valid() && m_cameraUniform.valid() && m_gridUniform.valid() &&
           m_displaceUniform.valid() && m_uvUniform.valid() && m_sphereUniform.valid() &&
           m_inputSampler.valid() && m_spectrumSampler.valid() && m_finalSampler.valid();
}

bool OceanFftGpu::uploadSpectrum(gfx::ResourceManager& resources, const OceanFFT& ocean) {
    if (!m_spectrumTexture.valid()) {
        return false;
    }
    const uint32_t resolution = ocean.resolution();
    const std::size_t texelCount = static_cast<std::size_t>(resolution) * resolution;
    bool uploaded = true;
    std::array<std::vector<uint16_t>, 2> layers;
    for (std::vector<uint16_t>& layer : layers) {
        layer.resize(texelCount * 4u, 0u);
    }
    ocean.visitSpectra([&](uint32_t gridIndex, const OceanFFT::Complex* spectrum, uint32_t count) {
        if (gridIndex >= 4 || spectrum == nullptr) {
            uploaded = false;
            return;
        }
        std::vector<uint16_t>& layer = layers[gridIndex / 2u];
        const std::size_t component = (gridIndex % 2u) * 2u;
        const uint32_t copyCount = std::min<uint32_t>(count, static_cast<uint32_t>(texelCount));
        for (uint32_t index = 0; index < copyCount; ++index) {
            layer[static_cast<std::size_t>(index) * 4u + component] = bx::halfFromFloat(spectrum[index].real());
            layer[static_cast<std::size_t>(index) * 4u + component + 1u] = bx::halfFromFloat(spectrum[index].imag());
        }
    });
    const uint32_t pitch = resolution * 4u * sizeof(uint16_t);
    const uint32_t size = pitch * resolution;
    for (uint16_t layer = 0; layer < static_cast<uint16_t>(layers.size()); ++layer) {
        uploaded = resources.updateTexture(m_spectrumTexture, layers[layer].data(), size,
                                            static_cast<uint16_t>(resolution), static_cast<uint16_t>(resolution),
                                            static_cast<uint16_t>(pitch), layer, 0) &&
                   uploaded;
    }
    return uploaded;
}

void OceanFftGpu::simulate(float timeSeconds, const OceanFFT& ocean) {
    if (m_resources == nullptr) {
        return;
    }
    const OceanFFTSettings& settings = ocean.settings();
    const Vec4 gridSizes(settings.gridSizes[0], settings.gridSizes[1], settings.gridSizes[2], settings.gridSizes[3]);
    const Vec4 timeParams(timeSeconds, 0.0f, 0.0f, 0.0f);

    gfx::Encoder encoder(*m_resources);
    encoder.setViewRect(kWaterFftView, 0, 0, static_cast<uint16_t>(m_resolution),
                        static_cast<uint16_t>(m_resolution));
    encoder.setViewFrameBuffer(kWaterFftView, {});
    encoder.setUniform(m_timeUniform, &timeParams, 1);
    encoder.setUniform(m_gridSizesUniform, &gridSizes, 1);

    encoder.setTexture(0, m_spectrumSampler, m_spectrumTexture);
    encoder.setImage(1, m_pingTexture, 0, static_cast<uint16_t>(kWaterFftWorkLayers), 0, bgfx::Access::Write,
                     gfx::TextureFormat::RGBA16F);
    encoder.dispatch(kWaterFftView, m_programs[static_cast<std::size_t>(Program::Prepare)],
                     (m_resolution + 7u) / 8u, (m_resolution + 7u) / 8u, 1);

    encoder.setTexture(0, m_inputSampler, m_pingTexture);
    encoder.setImage(1, m_pongTexture, 0, static_cast<uint16_t>(kWaterFftWorkLayers), 0, bgfx::Access::Write,
                     gfx::TextureFormat::RGBA16F);
    encoder.dispatch(kWaterFftView, m_programs[static_cast<std::size_t>(Program::Horizontal)], 1, m_resolution,
                     static_cast<uint32_t>(kWaterFftWorkLayers));

    encoder.setTexture(0, m_inputSampler, m_pongTexture);
    encoder.setImage(1, m_pingTexture, 0, static_cast<uint16_t>(kWaterFftWorkLayers), 0, bgfx::Access::Write,
                     gfx::TextureFormat::RGBA16F);
    encoder.dispatch(kWaterFftView, m_programs[static_cast<std::size_t>(Program::Vertical)], m_resolution, 1,
                     static_cast<uint32_t>(kWaterFftWorkLayers));

    encoder.setTexture(0, m_inputSampler, m_pingTexture);
    encoder.setImage(1, m_sourceTexture, 0, static_cast<uint16_t>(kWaterFftLayers), 0, bgfx::Access::Write,
                     gfx::TextureFormat::RGBA16F);
    encoder.dispatch(kWaterFftView, m_programs[static_cast<std::size_t>(Program::Finalize)],
                     (m_resolution + 7u) / 8u, (m_resolution + 7u) / 8u,
                     static_cast<uint32_t>(kWaterFftLayers));

    for (uint32_t mip = 0; mip < m_mipCount; ++mip) {
        const uint32_t size = std::max(m_resolution >> mip, 1u);
        const Vec4 mipParams(static_cast<float>(mip), static_cast<float>(size), 0.0f, 0.0f);
        encoder.setTexture(0, m_inputSampler, m_sourceTexture);
        encoder.setImage(1, m_finalTexture, 0, static_cast<uint16_t>(kWaterFftLayers), static_cast<uint8_t>(mip),
                         bgfx::Access::Write, gfx::TextureFormat::RGBA16F);
        encoder.setUniform(m_mipUniform, &mipParams, 1);
        encoder.dispatch(kWaterFftView, m_programs[static_cast<std::size_t>(Program::Mip)], (size + 7u) / 8u,
                         (size + 7u) / 8u, static_cast<uint32_t>(kWaterFftLayers));
    }
}

void OceanFftGpu::displace(gfx::ResourceManager& resources, const OceanFFT& ocean, gfx::BufferHandle vertexBuffer,
                           uint32_t vertexCount, int gridWidth, int gridHeight, float baseHeight,
                           const Mat4& invViewProj, const Vec3& cameraPosition, const Vec2& uvScale,
                           const Vec2& uvOrigin, float sphereRadius) {
    (void)resources;
    if (m_resources == nullptr) {
        return;
    }
    const OceanFFTSettings& settings = ocean.settings();
    const Vec4 gridSizes(settings.gridSizes[0], settings.gridSizes[1], settings.gridSizes[2], settings.gridSizes[3]);
    const Vec4 camera(cameraPosition.x, cameraPosition.y, cameraPosition.z, 0.0f);
    const Vec4 grid(static_cast<float>(gridWidth), static_cast<float>(gridHeight), baseHeight, 1.25f);
    const Vec4 displaceParams(settings.amplitude, settings.amplitude * settings.choppyFactor, 1.0e6f,
                              static_cast<float>(m_resolution));
    const Vec4 uvParams(uvScale.x, uvScale.y, uvOrigin.x, uvOrigin.y);
    const Vec4 sphereParams(sphereRadius, 0.0f, 0.0f, 0.0f);

    gfx::Encoder encoder(*m_resources);
    encoder.setViewRect(kWaterFftView, 0, 0, static_cast<uint16_t>(m_resolution),
                        static_cast<uint16_t>(m_resolution));
    encoder.setViewFrameBuffer(kWaterFftView, {});
    encoder.setTexture(0, m_finalSampler, m_finalTexture);
    encoder.setStorageBuffer(1, vertexBuffer, bgfx::Access::Write);
    encoder.setUniform(m_gridSizesUniform, &gridSizes, 1);
    encoder.setUniform(m_invViewProjUniform, invViewProj.values.data(), 1);
    encoder.setUniform(m_cameraUniform, &camera, 1);
    encoder.setUniform(m_gridUniform, &grid, 1);
    encoder.setUniform(m_displaceUniform, &displaceParams, 1);
    encoder.setUniform(m_uvUniform, &uvParams, 1);
    encoder.setUniform(m_sphereUniform, &sphereParams, 1);
    encoder.dispatch(kWaterFftView, m_programs[static_cast<std::size_t>(Program::Displace)],
                     (vertexCount + 63u) / 64u, 1, 1);
}

bool OceanFftGpu::update(gfx::ResourceManager& resources, const OceanFFT& ocean, gfx::BufferHandle vertexBuffer,
                         uint32_t vertexCount, int gridWidth, int gridHeight, float baseHeight,
                         const Mat4& invViewProj, const Vec3& cameraPosition, const Vec2& uvScale,
                         const Vec2& uvOrigin, float timeSeconds, float sphereRadius) {
    if (!m_valid || m_resources == nullptr || !vertexBuffer.valid() || vertexCount == 0 || gridWidth < 2 ||
        gridHeight < 2 || static_cast<uint32_t>(gridWidth) * static_cast<uint32_t>(gridHeight) != vertexCount) {
        return false;
    }

    const float interval = 1.0f / std::max(ocean.updateHz(), 0.001f);
    const bool simulateNow =
        !m_hasTime || timeSeconds < m_lastTime || (timeSeconds - m_lastTime) >= interval;
    if (simulateNow) {
        simulate(timeSeconds, ocean);
        m_lastTime = timeSeconds;
        m_hasTime = true;
        ++m_updateCount;
    }

    const bool viewChanged =
        !m_hasView ||
        !std::equal(invViewProj.values.begin(), invViewProj.values.end(), m_lastInvViewProj.values.begin()) ||
        cameraPosition != m_lastCamera;
    const bool layoutChanged = gridWidth != m_lastGridWidth || gridHeight != m_lastGridHeight ||
                               vertexCount != m_lastVertexCount || sphereRadius != m_lastSphereRadius;
    if (simulateNow || viewChanged || layoutChanged) {
        displace(resources, ocean, vertexBuffer, vertexCount, gridWidth, gridHeight, baseHeight, invViewProj,
                 cameraPosition, uvScale, uvOrigin, sphereRadius);
        m_lastInvViewProj = invViewProj;
        m_lastCamera = cameraPosition;
        m_lastGridWidth = gridWidth;
        m_lastGridHeight = gridHeight;
        m_lastVertexCount = vertexCount;
        m_lastSphereRadius = sphereRadius;
        m_hasView = true;
    }
    return true;
}

void OceanFftGpu::destroyTextures(gfx::ResourceManager& resources) {
    const auto destroy = [&](gfx::TextureHandle& texture) {
        if (texture.valid()) {
            resources.destroyTexture(texture);
            texture = {};
        }
    };
    destroy(m_spectrumTexture);
    destroy(m_pingTexture);
    destroy(m_pongTexture);
    destroy(m_sourceTexture);
    destroy(m_finalTexture);
}

void OceanFftGpu::destroyUniforms(gfx::ResourceManager& resources) {
    const auto destroy = [&](gfx::UniformHandle& uniform) {
        if (uniform.valid()) {
            resources.destroyUniform(uniform);
            uniform = {};
        }
    };
    destroy(m_timeUniform);
    destroy(m_gridSizesUniform);
    destroy(m_mipUniform);
    destroy(m_invViewProjUniform);
    destroy(m_cameraUniform);
    destroy(m_gridUniform);
    destroy(m_displaceUniform);
    destroy(m_uvUniform);
    destroy(m_sphereUniform);
    destroy(m_inputSampler);
    destroy(m_spectrumSampler);
    destroy(m_finalSampler);
}

void OceanFftGpu::destroyPrograms(gfx::ResourceManager& resources) {
    for (gfx::ProgramHandle& program : m_programs) {
        if (program.valid()) {
            resources.destroyProgram(program);
            program = {};
        }
    }
}

void OceanFftGpu::shutdown(gfx::ResourceManager& resources) {
    if (m_updateCount > 0) {
        HBG_LOGI("ocean fft gpu: updates={}", m_updateCount);
    }
    destroyPrograms(resources);
    destroyTextures(resources);
    destroyUniforms(resources);
    m_resources = nullptr;
    m_valid = false;
    m_hasTime = false;
    m_hasView = false;
    m_lastTime = -1.0f;
    m_lastGridWidth = 0;
    m_lastGridHeight = 0;
    m_lastVertexCount = 0;
    m_lastSphereRadius = 0.0f;
    m_resolution = 0;
    m_mipCount = 0;
    m_updateCount = 0;
}

} // namespace hbg::render
