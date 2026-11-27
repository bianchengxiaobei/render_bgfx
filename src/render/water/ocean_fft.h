#pragma once

#include "asset/mesh.h"

#include <array>
#include <complex>
#include <cstdint>
#include <functional>
#include <vector>

namespace hbg::render {

struct OceanFFTSettings {
    uint32_t resolution = 128;
    float tileSize = 5488.0f;
    float amplitude = 1.0f;
    float updateHz = 24.0f;
    std::array<float, 4> gridSizes = {5488.0f, 392.0f, 28.0f, 2.0f};
    float windSpeed = 5.0f;
    float omega = 0.84f;
    float choppyFactor = 2.5f;
    uint32_t seed = 1234;
};

class OceanFFT {
  public:
    using Complex = std::complex<float>;
    using SlopeVarianceLut = std::array<Vec4, 250>;
    using TextureLayerVisitor =
        std::function<void(uint16_t layer, uint8_t mip, const float* data, uint16_t width, uint16_t height)>;
    using SpectrumVisitor =
        std::function<void(uint32_t gridIndex, const Complex* spectrum, uint32_t count)>;

    bool init(const OceanFFTSettings& settings);
    bool update(float timeSeconds);
    void applyToVertices(std::vector<asset::MeshVertex>& vertices, float baseHeight,
                         const std::vector<asset::MeshVertex>* baseVertices = nullptr) const;
    void applyToScreenGrid(std::vector<asset::MeshVertex>& vertices, float baseHeight, int gridWidth, int gridHeight,
                           const Mat4& invViewProj, const Vec3& cameraPosition, const Vec2& uvScale,
                           const Vec2& uvOrigin, float sphereRadius = 0.0f) const;
    void visitTextureLayers(const TextureLayerVisitor& visitor) const;
    void visitSpectra(const SpectrumVisitor& visitor) const;

    bool valid() const { return m_valid; }
    const OceanFFTSettings& settings() const { return m_settings; }
    float updateHz() const { return m_settings.updateHz; }
    uint32_t resolution() const { return m_settings.resolution; }
    uint32_t mipLevelCount() const { return m_mipLevelCount; }
    uint32_t textureLayerCount() const { return 5u; }
    const std::array<float, 4>& gridSizes() const { return m_settings.gridSizes; }
    uint32_t updateCount() const { return m_updateCount; }
    float maxSlopeVariance() const { return m_maxSlopeVariance; }
    const SlopeVarianceLut& slopeVarianceLut() const { return m_slopeVarianceLut; }
    bool slopeVarianceLutValid() const { return m_valid; }

  private:
    struct Grid {
        float size = 1.0f;
        std::vector<Complex> spectrum;
        std::vector<float> omega;
        std::vector<float> height;
        std::vector<float> slopeX;
        std::vector<float> slopeZ;
        std::vector<float> choppyX;
        std::vector<float> choppyZ;
        std::array<std::vector<std::vector<float>>, 5> mips;
    };

    float spectrum(float kx, float ky, bool omnispectrum = false) const;
    float waveOmega(float k) const;
    void generateSpectrum(Grid& grid);
    void computeSlopeVarianceStats();
    void inverseFft2(std::vector<Complex>& data) const;
    void updateGrid(Grid& grid, float timeSeconds) const;
    void buildMipChain(Grid& grid) const;
    float sample(const std::vector<std::vector<float>>& mips, float x, float z, float size, float lod) const;

    struct WaveSample {
        float height = 0.0f;
        float slopeX = 0.0f;
        float slopeZ = 0.0f;
        float choppyX = 0.0f;
        float choppyZ = 0.0f;
    };

    WaveSample sampleWaves(float x, float z, float footprint = 0.0f) const;

    OceanFFTSettings m_settings;
    std::array<Grid, 4> m_grids;
    uint32_t m_mipLevelCount = 0;
    SlopeVarianceLut m_slopeVarianceLut{};
    float m_maxSlopeVariance = 0.0f;
    bool m_valid = false;
    float m_lastUpdateTime = -1.0f;
    uint32_t m_updateCount = 0;
};

} // namespace hbg::render
