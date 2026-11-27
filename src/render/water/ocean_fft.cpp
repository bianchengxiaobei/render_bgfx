#include "render/water/ocean_fft.h"

#include "core/log.h"
#include "render/water/ocean_sphere.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>

namespace hbg::render {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kGravity = 9.81f;
constexpr float kCm = 0.23f;
constexpr float kKm = 370.0f;

float square(float value) {
    return value * value;
}

Vec4 transformVec4(const Mat4& matrix, const Vec4& value) {
    return Vec4(matrix.values[0] * value.x + matrix.values[4] * value.y + matrix.values[8] * value.z +
                    matrix.values[12] * value.w,
                matrix.values[1] * value.x + matrix.values[5] * value.y + matrix.values[9] * value.z +
                    matrix.values[13] * value.w,
                matrix.values[2] * value.x + matrix.values[6] * value.y + matrix.values[10] * value.z +
                    matrix.values[14] * value.w,
                matrix.values[3] * value.x + matrix.values[7] * value.y + matrix.values[11] * value.z +
                    matrix.values[15] * value.w);
}

} // namespace

bool OceanFFT::init(const OceanFFTSettings& settings) {
    m_settings = settings;
    m_valid = false;
    m_lastUpdateTime = -1.0f;
    m_updateCount = 0;

    const uint32_t resolution = m_settings.resolution;
    if (resolution < 16 || resolution > 512 || (resolution & (resolution - 1)) != 0) {
        HBG_LOGE("ocean fft: resolution must be a power of two in [16, 512] (got {})", resolution);
        return false;
    }
    if (m_settings.amplitude < 0.0f || m_settings.updateHz <= 0.0f || m_settings.choppyFactor < 0.0f) {
        HBG_LOGE("ocean fft: invalid amplitude, choppy factor, or update rate");
        return false;
    }

    m_mipLevelCount = 1;
    for (uint32_t size = resolution; size > 1; size >>= 1) {
        ++m_mipLevelCount;
    }

    for (std::size_t index = 0; index < m_grids.size(); ++index) {
        Grid& grid = m_grids[index];
        grid.size = m_settings.gridSizes[index];
        if (grid.size <= 0.0f) {
            HBG_LOGE("ocean fft: grid {} has an invalid size", index);
            return false;
        }
        grid.spectrum.assign(static_cast<std::size_t>(resolution) * resolution, Complex{});
        grid.omega.assign(static_cast<std::size_t>(resolution) * resolution, 0.0f);
        grid.height.assign(static_cast<std::size_t>(resolution) * resolution, 0.0f);
        grid.slopeX.assign(static_cast<std::size_t>(resolution) * resolution, 0.0f);
        grid.slopeZ.assign(static_cast<std::size_t>(resolution) * resolution, 0.0f);
        grid.choppyX.assign(static_cast<std::size_t>(resolution) * resolution, 0.0f);
        grid.choppyZ.assign(static_cast<std::size_t>(resolution) * resolution, 0.0f);
        generateSpectrum(grid);
    }
    computeSlopeVarianceStats();

    m_valid = true;
    HBG_LOGI("ocean fft: resolution={} mips={} grids={:.1f}/{:.1f}/{:.1f}/{:.1f}m update={:.1f}Hz "
             "amplitude={:.2f} choppy={:.2f}",
             resolution, m_mipLevelCount, m_settings.gridSizes[0], m_settings.gridSizes[1], m_settings.gridSizes[2],
             m_settings.gridSizes[3], m_settings.updateHz, m_settings.amplitude, m_settings.choppyFactor);
    return true;
}

float OceanFFT::waveOmega(float k) const {
    if (k <= std::numeric_limits<float>::epsilon()) {
        return 0.0f;
    }
    return std::sqrt(kGravity * k * (1.0f + square(k / kKm)));
}

float OceanFFT::spectrum(float kx, float ky, bool omnispectrum) const {
    const float k = std::sqrt(kx * kx + ky * ky);
    if (k <= std::numeric_limits<float>::epsilon()) {
        return 0.0f;
    }

    const float wind = std::max(m_settings.windSpeed, 0.01f);
    const float seaState = std::max(m_settings.omega, 0.01f);
    const float c = waveOmega(k) / k;
    const float kp = kGravity * square(seaState / wind);
    const float cp = waveOmega(kp) / kp;
    const float z0 = 3.7e-5f * square(wind) / kGravity * std::pow(wind / cp, 0.9f);
    const float uStar = 0.41f * wind / std::max(std::log(10.0f / std::max(z0, 1.0e-8f)), 1.0e-6f);

    const float lpm = std::exp(-1.25f * square(kp / k));
    const float gamma = seaState < 1.0f ? 1.7f : 1.7f + 6.0f * std::log(seaState);
    const float sigma = 0.08f * (1.0f + 4.0f / std::pow(seaState, 3.0f));
    const float gammaTerm = std::exp(-1.0f / (2.0f * square(sigma)) * square(std::sqrt(k / kp) - 1.0f));
    const float jp = std::pow(gamma, gammaTerm);
    const float fp = lpm * jp * std::exp(-seaState / std::sqrt(10.0f) * (std::sqrt(k / kp) - 1.0f));
    const float alphaP = 0.006f * std::sqrt(seaState);
    float bl = 0.5f * alphaP * cp / c * fp;

    const float alphaM = 0.01f * (uStar < kCm ? 1.0f + std::log(uStar / kCm) : 1.0f + 3.0f * std::log(uStar / kCm));
    const float fm = std::exp(-0.25f * square(k / kKm - 1.0f));
    float bh = 0.5f * alphaM * kCm / c * fm;
    bh *= lpm;

    if (omnispectrum) {
        return (bl + bh) / (k * square(k));
    }

    const float a0 = std::log(2.0f) / 4.0f;
    const float ap = 4.0f;
    const float am = 0.13f * uStar / kCm;
    const float delta = std::tanh(a0 + ap * std::pow(c / cp, 2.5f) + am * std::pow(kCm / c, 2.5f));
    const float phi = std::atan2(ky, kx);
    return (bl + bh) * (1.0f + delta * std::cos(2.0f * phi)) / (2.0f * kPi * square(k * k));
}

void OceanFFT::computeSlopeVarianceStats() {
    const auto started = std::chrono::steady_clock::now();
    m_maxSlopeVariance = 0.0f;
    m_slopeVarianceLut.fill(Vec4{});

    float theoreticSlopeVariance = 0.0f;
    for (float k = 5.0e-3f; k < 1.0e3f;) {
        const float nextK = k * 1.001f;
        theoreticSlopeVariance += k * k * spectrum(k, 0.0f, true) * (nextK - k);
        k = nextK;
    }

    float totalSlopeVariance = 0.0f;
    const int resolution = static_cast<int>(m_settings.resolution);
    std::array<std::vector<float>, 4> spectra;
    for (std::size_t gridIndex = 0; gridIndex < m_grids.size(); ++gridIndex) {
        const Grid& grid = m_grids[gridIndex];
        std::vector<float>& h2 = spectra[gridIndex];
        h2.resize(static_cast<std::size_t>(resolution) * resolution);
        const float dk = 2.0f * kPi / grid.size;
        for (int y = 0; y < resolution; ++y) {
            const int j = y < resolution / 2 ? y : y - resolution;
            for (int x = 0; x < resolution; ++x) {
                const int i = x < resolution / 2 ? x : x - resolution;
                const float kx = static_cast<float>(i) * dk;
                const float ky = static_cast<float>(j) * dk;
                const std::size_t index = static_cast<std::size_t>(y) * resolution + x;
                const float magnitudeSquared = square(std::abs(grid.spectrum[index]));
                h2[index] = magnitudeSquared;
                totalSlopeVariance += (kx * kx + ky * ky) * magnitudeSquared * 2.0f;
            }
        }
    }

    constexpr int kLutSize = 10;
    constexpr float kLutScale = 10.0f;
    const float slopeVarianceDelta = theoreticSlopeVariance - totalSlopeVariance;
    std::array<float, kLutSize * kLutSize * kLutSize> values{};
    float lutMax = 0.0f;

    for (int c = 0; c < kLutSize; ++c) {
        const float cRaw = std::pow(static_cast<float>(c) / (kLutSize - 1), 4.0f) * kLutScale;
        for (int b = 0; b < kLutSize; ++b) {
            const float bRaw = (2.0f * static_cast<float>(b) / (kLutSize - 1) - 1.0f);
            for (int a = 0; a < kLutSize; ++a) {
                const float aRaw = std::pow(static_cast<float>(a) / (kLutSize - 1), 4.0f) * kLutScale;
                const float A = -0.5f * aRaw;
                const float B = -bRaw * std::sqrt(aRaw * cRaw);
                const float C = -0.5f * cRaw;

                float slopeVariance = slopeVarianceDelta;
                for (std::size_t gridIndex = 0; gridIndex < m_grids.size(); ++gridIndex) {
                    const Grid& grid = m_grids[gridIndex];
                    const std::vector<float>& h2 = spectra[gridIndex];
                    const float invSize = 1.0f / grid.size;
                    for (int y = 0; y < resolution; ++y) {
                        const int j = y < resolution / 2 ? y : y - resolution;
                        const float ky = 2.0f * kPi * static_cast<float>(j) * invSize;
                        for (int x = 0; x < resolution; ++x) {
                            const int i = x < resolution / 2 ? x : x - resolution;
                            const float kx = 2.0f * kPi * static_cast<float>(i) * invSize;
                            const float w = 1.0f - std::exp(A * kx * kx + B * kx * ky + C * ky * ky);
                            const float kxw = kx * w;
                            const float kyw = ky * w;
                            slopeVariance +=
                                (kxw * kxw + kyw * kyw) * h2[static_cast<std::size_t>(y) * resolution + x] * 2.0f;
                        }
                    }
                }

                const std::size_t index = static_cast<std::size_t>((c * kLutSize + b) * kLutSize + a);
                values[index] = slopeVariance;
                lutMax = std::max(lutMax, slopeVariance);
            }
        }
    }

    for (std::size_t index = 0; index < values.size(); ++index) {
        Vec4& texel = m_slopeVarianceLut[index / 4];
        switch (index % 4) {
        case 0:
            texel.x = values[index];
            break;
        case 1:
            texel.y = values[index];
            break;
        case 2:
            texel.z = values[index];
            break;
        default:
            texel.w = values[index];
            break;
        }
    }

    m_maxSlopeVariance = std::max(theoreticSlopeVariance, totalSlopeVariance);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    HBG_LOGI("ocean fft: slope variance max={:.5f} theoretic={:.5f} grid={:.5f} "
             "lutMax={:.5f} lutTime={:.2f}s",
             m_maxSlopeVariance, theoreticSlopeVariance, totalSlopeVariance, lutMax, elapsed);
}

void OceanFFT::generateSpectrum(Grid& grid) {
    const int resolution = static_cast<int>(m_settings.resolution);
    const float dk = 2.0f * kPi / grid.size;
    std::mt19937 rng(m_settings.seed + static_cast<uint32_t>(grid.size));
    std::uniform_real_distribution<float> phaseDistribution(0.0f, 2.0f * kPi);
    const float kMin = kPi / grid.size;

    for (int y = 0; y < resolution; ++y) {
        const int j = y < resolution / 2 ? y : y - resolution;
        for (int x = 0; x < resolution; ++x) {
            const int i = x < resolution / 2 ? x : x - resolution;
            const float kx = static_cast<float>(i) * dk;
            const float ky = static_cast<float>(j) * dk;
            const float magnitude = std::sqrt(kx * kx + ky * ky);

            grid.omega[static_cast<std::size_t>(y) * resolution + x] = waveOmega(magnitude);
            if (i < 0 || (i == 0 && j <= 0)) {
                continue;
            }

            float amplitude = 0.0f;
            if (magnitude >= kMin) {
                amplitude = std::sqrt(std::max(spectrum(kx, ky), 0.0f) * 0.5f) * dk;
            }
            const float phase = phaseDistribution(rng);
            const Complex value = std::polar(amplitude, phase);
            const std::size_t positiveIndex = static_cast<std::size_t>(y) * resolution + x;
            grid.spectrum[positiveIndex] = value;

            const int mirroredX = (resolution - i) % resolution;
            const int mirroredY = (resolution - j) % resolution;
            const std::size_t mirroredIndex = static_cast<std::size_t>(mirroredY) * resolution + mirroredX;
            grid.spectrum[mirroredIndex] = std::conj(value);
        }
    }
}

void OceanFFT::inverseFft2(std::vector<Complex>& data) const {
    const int resolution = static_cast<int>(m_settings.resolution);
    const auto inverseFft1 = [resolution](std::vector<Complex>& line) {
        for (int i = 1, j = 0; i < resolution; ++i) {
            int bit = resolution >> 1;
            for (; (j & bit) != 0; bit >>= 1) {
                j ^= bit;
            }
            j ^= bit;
            if (i < j) {
                std::swap(line[static_cast<std::size_t>(i)], line[static_cast<std::size_t>(j)]);
            }
        }

        for (int length = 2; length <= resolution; length <<= 1) {
            const float angle = 2.0f * kPi / static_cast<float>(length);
            const Complex step(std::cos(angle), std::sin(angle));
            for (int offset = 0; offset < resolution; offset += length) {
                Complex weight(1.0f, 0.0f);
                for (int index = 0; index < length / 2; ++index) {
                    const Complex even = line[static_cast<std::size_t>(offset + index)];
                    const Complex odd = line[static_cast<std::size_t>(offset + index + length / 2)] * weight;
                    line[static_cast<std::size_t>(offset + index)] = even + odd;
                    line[static_cast<std::size_t>(offset + index + length / 2)] = even - odd;
                    weight *= step;
                }
            }
        }
    };

    std::vector<Complex> scratch(static_cast<std::size_t>(resolution));
    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            scratch[static_cast<std::size_t>(x)] = data[static_cast<std::size_t>(y) * resolution + x];
        }
        inverseFft1(scratch);
        for (int x = 0; x < resolution; ++x) {
            data[static_cast<std::size_t>(y) * resolution + x] = scratch[static_cast<std::size_t>(x)];
        }
    }

    for (int x = 0; x < resolution; ++x) {
        for (int y = 0; y < resolution; ++y) {
            scratch[static_cast<std::size_t>(y)] = data[static_cast<std::size_t>(y) * resolution + x];
        }
        inverseFft1(scratch);
        for (int y = 0; y < resolution; ++y) {
            data[static_cast<std::size_t>(y) * resolution + x] = scratch[static_cast<std::size_t>(y)];
        }
    }
}

void OceanFFT::updateGrid(Grid& grid, float timeSeconds) const {
    const int resolution = static_cast<int>(m_settings.resolution);
    const float dk = 2.0f * kPi / grid.size;
    std::vector<Complex> work = grid.spectrum;
    std::vector<Complex> slope = grid.spectrum;
    std::vector<Complex> choppy = grid.spectrum;
    for (int y = 0; y < resolution; ++y) {
        const int j = y < resolution / 2 ? y : y - resolution;
        for (int x = 0; x < resolution; ++x) {
            const int i = x < resolution / 2 ? x : x - resolution;
            const std::size_t index = static_cast<std::size_t>(y) * resolution + x;
            const float direction = (i > 0 || (i == 0 && j > 0)) ? 1.0f : -1.0f;
            const float phase = direction * grid.omega[index] * timeSeconds;
            const Complex h = grid.spectrum[index] * Complex(std::cos(phase), std::sin(phase));
            work[index] = h;

            const float kx = static_cast<float>(i) * dk;
            const float kz = static_cast<float>(j) * dk;
            const float k2 = kx * kx + kz * kz;
            Complex slopeSpectrum(0.0f, 0.0f);
            Complex choppySpectrum(0.0f, 0.0f);
            if (k2 > 1.0e-12f) {
                const float invK = 1.0f / std::sqrt(k2);
                slopeSpectrum = h * Complex(-kz, kx);
                choppySpectrum = slopeSpectrum * invK;
            }
            slope[index] = slopeSpectrum;
            choppy[index] = choppySpectrum;
        }
    }
    inverseFft2(work);
    inverseFft2(slope);
    inverseFft2(choppy);

    for (int y = 0; y < resolution; ++y) {
        for (int x = 0; x < resolution; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * resolution + x;
            grid.height[index] = work[index].real();
            grid.slopeX[index] = slope[index].real();
            grid.slopeZ[index] = slope[index].imag();
            grid.choppyX[index] = choppy[index].real();
            grid.choppyZ[index] = choppy[index].imag();
        }
    }
    buildMipChain(grid);
}

void OceanFFT::visitTextureLayers(const TextureLayerVisitor& visitor) const {
    if (!m_valid || !visitor || m_mipLevelCount == 0) {
        return;
    }

    for (const Grid& grid : m_grids) {
        if (grid.mips[0].size() != m_mipLevelCount) {
            return;
        }
    }

    std::vector<float> packed;
    const int baseResolution = static_cast<int>(m_settings.resolution);
    for (uint32_t mip = 0; mip < m_mipLevelCount; ++mip) {
        const uint16_t size = static_cast<uint16_t>(std::max(baseResolution >> mip, 1));
        const std::size_t texelCount = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
        packed.resize(texelCount * 4u);

        const auto uploadLayer = [&](uint16_t layer,
                                     const std::array<const std::vector<float>*, 4>& fields) {
            for (std::size_t texel = 0; texel < texelCount; ++texel) {
                const std::size_t offset = texel * 4u;
                packed[offset + 0] = (*fields[0])[texel];
                packed[offset + 1] = (*fields[1])[texel];
                packed[offset + 2] = (*fields[2])[texel];
                packed[offset + 3] = (*fields[3])[texel];
            }
            visitor(layer, static_cast<uint8_t>(mip), packed.data(), size, size);
        };

        uploadLayer(0, {&m_grids[0].mips[0][mip], &m_grids[1].mips[0][mip], &m_grids[2].mips[0][mip],
                        &m_grids[3].mips[0][mip]});
        uploadLayer(1, {&m_grids[0].mips[1][mip], &m_grids[0].mips[2][mip], &m_grids[1].mips[1][mip],
                        &m_grids[1].mips[2][mip]});
        uploadLayer(2, {&m_grids[2].mips[1][mip], &m_grids[2].mips[2][mip], &m_grids[3].mips[1][mip],
                        &m_grids[3].mips[2][mip]});
        uploadLayer(3, {&m_grids[0].mips[3][mip], &m_grids[0].mips[4][mip], &m_grids[1].mips[3][mip],
                        &m_grids[1].mips[4][mip]});
        uploadLayer(4, {&m_grids[2].mips[3][mip], &m_grids[2].mips[4][mip], &m_grids[3].mips[3][mip],
                        &m_grids[3].mips[4][mip]});
    }
}

void OceanFFT::visitSpectra(const SpectrumVisitor& visitor) const {
    if (!m_valid || !visitor) {
        return;
    }
    const uint32_t count = m_settings.resolution * m_settings.resolution;
    for (uint32_t gridIndex = 0; gridIndex < static_cast<uint32_t>(m_grids.size()); ++gridIndex) {
        visitor(gridIndex, m_grids[gridIndex].spectrum.data(), count);
    }
}

void OceanFFT::buildMipChain(Grid& grid) const {
    const std::array<std::vector<float>*, 5> fields = {&grid.height, &grid.slopeX, &grid.slopeZ, &grid.choppyX,
                                                       &grid.choppyZ};
    const int resolution = static_cast<int>(m_settings.resolution);

    for (std::size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
        std::vector<std::vector<float>>& mips = grid.mips[fieldIndex];
        mips.resize(m_mipLevelCount);
        mips[0] = *fields[fieldIndex];

        int sourceResolution = resolution;
        for (uint32_t level = 1; level < m_mipLevelCount; ++level) {
            const int targetResolution = std::max(sourceResolution / 2, 1);
            std::vector<float>& source = mips[level - 1];
            std::vector<float>& target = mips[level];
            target.assign(static_cast<std::size_t>(targetResolution) * targetResolution, 0.0f);

            for (int y = 0; y < targetResolution; ++y) {
                const int y0 = std::min(y * 2, sourceResolution - 1);
                const int y1 = std::min(y0 + 1, sourceResolution - 1);
                for (int x = 0; x < targetResolution; ++x) {
                    const int x0 = std::min(x * 2, sourceResolution - 1);
                    const int x1 = std::min(x0 + 1, sourceResolution - 1);
                    const auto at = [&](int sx, int sy) {
                        return source[static_cast<std::size_t>(sy) * sourceResolution + sx];
                    };
                    target[static_cast<std::size_t>(y) * targetResolution + x] =
                        0.25f * (at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1));
                }
            }
            sourceResolution = targetResolution;
        }
    }
}

bool OceanFFT::update(float timeSeconds) {
    if (!m_valid) {
        return false;
    }
    const float interval = 1.0f / std::max(m_settings.updateHz, 0.001f);
    if (m_lastUpdateTime >= 0.0f && timeSeconds >= m_lastUpdateTime && timeSeconds - m_lastUpdateTime < interval) {
        return false;
    }
    m_lastUpdateTime = timeSeconds;
    for (Grid& grid : m_grids) {
        updateGrid(grid, timeSeconds);
    }
    ++m_updateCount;
    return true;
}

float OceanFFT::sample(const std::vector<std::vector<float>>& mips, float x, float z, float size, float lod) const {
    if (mips.empty() || mips[0].empty()) {
        return 0.0f;
    }

    const int baseResolution = static_cast<int>(m_settings.resolution);
    const float clampedLod = std::clamp(lod, 0.0f, static_cast<float>(mips.size() - 1));
    const int level0 = static_cast<int>(std::floor(clampedLod));
    const int level1 = std::min(level0 + 1, static_cast<int>(mips.size() - 1));
    const float levelBlend = clampedLod - static_cast<float>(level0);

    const auto sampleLevel = [&](int level) {
        const int resolution = std::max(baseResolution >> level, 1);
        const float u = x / size - std::floor(x / size);
        const float v = z / size - std::floor(z / size);
        const float fx = u * static_cast<float>(resolution);
        const float fz = v * static_cast<float>(resolution);
        const int x0 = static_cast<int>(std::floor(fx));
        const int z0 = static_cast<int>(std::floor(fz));
        const int xi0 = (x0 % resolution + resolution) % resolution;
        const int zi0 = (z0 % resolution + resolution) % resolution;
        const int x1 = (xi0 + 1) % resolution;
        const int z1 = (zi0 + 1) % resolution;
        const float tx = fx - static_cast<float>(x0);
        const float tz = fz - static_cast<float>(z0);
        const std::vector<float>& field = mips[static_cast<std::size_t>(level)];
        const auto at = [&](int sx, int sz) { return field[static_cast<std::size_t>(sz) * resolution + sx]; };
        const float h00 = at(xi0, zi0);
        const float h10 = at(x1, zi0);
        const float h01 = at(xi0, z1);
        const float h11 = at(x1, z1);
        return (h00 + (h10 - h00) * tx) + ((h01 + (h11 - h01) * tx) - (h00 + (h10 - h00) * tx)) * tz;
    };

    const float a = sampleLevel(level0);
    if (level1 == level0) {
        return a;
    }
    return a + (sampleLevel(level1) - a) * levelBlend;
}

OceanFFT::WaveSample OceanFFT::sampleWaves(float x, float z, float footprint) const {
    WaveSample result;
    for (const Grid& grid : m_grids) {
        const float gridTexelSize = grid.size / static_cast<float>(m_settings.resolution);
        const float lod =
            footprint > gridTexelSize && gridTexelSize > 0.0f ? std::log2(footprint / gridTexelSize) : 0.0f;
        result.height += sample(grid.mips[0], x, z, grid.size, lod);
        result.slopeX += sample(grid.mips[1], x, z, grid.size, lod);
        result.slopeZ += sample(grid.mips[2], x, z, grid.size, lod);
        result.choppyX += sample(grid.mips[3], x, z, grid.size, lod);
        result.choppyZ += sample(grid.mips[4], x, z, grid.size, lod);
    }
    return result;
}

void OceanFFT::applyToVertices(std::vector<asset::MeshVertex>& vertices, float baseHeight,
                               const std::vector<asset::MeshVertex>* baseVertices) const {
    if (!m_valid) {
        return;
    }
    const bool hasBase = baseVertices != nullptr && baseVertices->size() == vertices.size();
    const float choppyScale = m_settings.amplitude * m_settings.choppyFactor;
    for (std::size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex) {
        asset::MeshVertex& vertex = vertices[vertexIndex];
        const Vec3 base = hasBase ? (*baseVertices)[vertexIndex].position : vertex.position;
        const WaveSample waves = sampleWaves(base.x, base.z);
        const Vec3 normal(-waves.slopeX, 1.0f, -waves.slopeZ);
        const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        vertex.position.x = base.x + waves.choppyX * choppyScale;
        vertex.position.y = baseHeight + waves.height * m_settings.amplitude;
        vertex.position.z = base.z + waves.choppyZ * choppyScale;
        vertex.normal = length > 1.0e-6f ? normal * (1.0f / length) : Vec3(0.0f, 1.0f, 0.0f);
    }
}

void OceanFFT::applyToScreenGrid(std::vector<asset::MeshVertex>& vertices, float baseHeight, int gridWidth,
                                 int gridHeight, const Mat4& invViewProj, const Vec3& cameraPosition,
                                 const Vec2& uvScale, const Vec2& uvOrigin, float sphereRadius) const {
    if (!m_valid || gridWidth < 2 || gridHeight < 2 ||
        vertices.size() != static_cast<std::size_t>(gridWidth) * static_cast<std::size_t>(gridHeight)) {
        return;
    }

    constexpr float kOverscan = 1.25f;
    constexpr float kMaxDistance = 1.0e6f;
    const float denominatorX = static_cast<float>(gridWidth - 1);
    const float denominatorY = static_cast<float>(gridHeight - 1);
    const float choppyScale = m_settings.amplitude * m_settings.choppyFactor;
    const std::size_t vertexCount = vertices.size();
    std::vector<Vec3> bases(vertexCount);

    for (int row = 0; row < gridHeight; ++row) {
        const float ndcY = 2.0f * kOverscan * static_cast<float>(row) / denominatorY - kOverscan;
        for (int column = 0; column < gridWidth; ++column) {
            const float ndcX = 2.0f * kOverscan * static_cast<float>(column) / denominatorX - kOverscan;
            const std::size_t index =
                static_cast<std::size_t>(row) * static_cast<std::size_t>(gridWidth) + static_cast<std::size_t>(column);

            const Vec4 world = transformVec4(invViewProj, Vec4(ndcX, ndcY, 1.0f, 1.0f));
            Vec3 ray(0.0f, 0.0f, -1.0f);
            if (std::abs(world.w) > 1.0e-8f) {
                const Vec3 farPoint(world.x / world.w, world.y / world.w, world.z / world.w);
                ray = normalize(farPoint - cameraPosition);
            }

            Vec3 base;
            bool intersects = false;
            if (sphereRadius > 0.0f) {
                const OceanSphereHit hit = intersectOceanSphere(cameraPosition, ray, sphereRadius, baseHeight);
                if (hit.valid) {
                    base = hit.position;
                    intersects = true;
                }
            }
            if (!intersects && ray.y < -1.0e-5f) {
                const float distance = (baseHeight - cameraPosition.y) / ray.y;
                if (distance > 0.0f && distance < kMaxDistance) {
                    base = cameraPosition + ray * distance;
                    base.y = baseHeight;
                    intersects = true;
                }
            }
            if (!intersects) {
                const Vec3 horizontal = normalize(Vec3(ray.x, 0.0f, ray.z));
                base = cameraPosition + horizontal * kMaxDistance;
                base.y = baseHeight;
            }
            bases[index] = base;
        }
    }

    for (int row = 0; row < gridHeight; ++row) {
        const int rowPrev = std::max(row - 1, 0);
        const int rowNext = std::min(row + 1, gridHeight - 1);
        for (int column = 0; column < gridWidth; ++column) {
            const int columnPrev = std::max(column - 1, 0);
            const int columnNext = std::min(column + 1, gridWidth - 1);
            const std::size_t index =
                static_cast<std::size_t>(row) * static_cast<std::size_t>(gridWidth) + static_cast<std::size_t>(column);
            const Vec3 base = bases[index];
            const auto at = [&](int columnIndex, int rowIndex) {
                return bases[static_cast<std::size_t>(rowIndex) * static_cast<std::size_t>(gridWidth) +
                             static_cast<std::size_t>(columnIndex)];
            };
            const float footprintX = length(at(columnNext, row) - at(columnPrev, row));
            const float footprintZ = length(at(column, rowNext) - at(column, rowPrev));
            const float footprint = std::max(footprintX, footprintZ);

            const WaveSample waves = sampleWaves(base.x, base.z, footprint);
            asset::MeshVertex& vertex = vertices[index];

            const Vec2 sphereSlope = oceanSphereSlope(base, cameraPosition, sphereRadius, baseHeight);
            const Vec3 normal(-waves.slopeX + sphereSlope.x, 1.0f, -waves.slopeZ + sphereSlope.y);
            const float normalLength = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            vertex.position.x = base.x + waves.choppyX * choppyScale;
            vertex.position.y = (sphereRadius > 0.0f ? base.y : baseHeight) + waves.height * m_settings.amplitude;
            vertex.position.z = base.z + waves.choppyZ * choppyScale;
            vertex.normal = normalLength > 1.0e-6f ? normal * (1.0f / normalLength) : Vec3(0.0f, 1.0f, 0.0f);
            vertex.tangent = Vec4(1.0f, 0.0f, 0.0f, -1.0f);
            vertex.texcoord0 = Vec2(base.x * uvScale.x + uvOrigin.x, base.z * uvScale.y + uvOrigin.y);
            vertex.texcoord1 = Vec2(0.0f, 0.0f);
            vertex.color = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }
}

} // namespace hbg::render
