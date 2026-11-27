#include "render/river/river_particles.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace hbg::render {
namespace {

constexpr float kRiverWaveSlopeScale = 2.88f;

Vec2 subtract(Vec2 lhs, Vec2 rhs) {
    return Vec2(lhs.x - rhs.x, lhs.y - rhs.y);
}

Vec2 add(Vec2 lhs, Vec2 rhs) {
    return Vec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

Vec2 scale(Vec2 value, float factor) {
    return Vec2(value.x * factor, value.y * factor);
}

float dot(Vec2 lhs, Vec2 rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

float length(Vec2 value) {
    return std::sqrt(dot(value, value));
}

float lengthSq(Vec2 value) {
    return dot(value, value);
}

Vec2 normalize(Vec2 value) {
    const float magnitude = length(value);
    return magnitude > 1.0e-8f ? scale(value, 1.0f / magnitude) : Vec2(0.0f, 1.0f);
}

float smoothStep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1.0e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float smoothFunc(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr float kSmallestAngleRange = 0.000001f;

struct AngleRange {
    float minAngle = 0.0f;
    float maxAngle = 0.0f;
};

// Port of Proland's RangeList: subtract angular cones from the available
// directions around a particle, wrapping at 2*pi. ScreenParticleLayer uses
// this to choose a Poisson-disk spawn point at 2 * radius from an existing
// particle.
class AngleRangeList {
  public:
    void reset() {
        m_ranges.assign(1, AngleRange{0.0f, kTwoPi});
        m_scratch.clear();
    }

    int count() const { return static_cast<int>(m_ranges.size()); }

    const AngleRange& at(int index) const { return m_ranges[static_cast<std::size_t>(index)]; }

    void subtract(float minAngle, float maxAngle) {
        if (maxAngle - minAngle <= 0.0f) {
            return;
        }
        if (maxAngle - minAngle >= kTwoPi) {
            m_ranges.clear();
            return;
        }

        while (minAngle < 0.0f) {
            minAngle += kTwoPi;
            maxAngle += kTwoPi;
        }
        while (minAngle >= kTwoPi) {
            minAngle -= kTwoPi;
            maxAngle -= kTwoPi;
        }

        if (maxAngle > kTwoPi) {
            subtractArc(minAngle, kTwoPi);
            subtractArc(0.0f, maxAngle - kTwoPi);
        } else {
            subtractArc(minAngle, maxAngle);
        }
    }

  private:
    void subtractArc(float minAngle, float maxAngle) {
        m_scratch.clear();
        for (const AngleRange& range : m_ranges) {
            if (range.maxAngle <= minAngle || range.minAngle >= maxAngle) {
                m_scratch.push_back(range);
                continue;
            }
            if (range.minAngle < minAngle && minAngle - range.minAngle > kSmallestAngleRange) {
                m_scratch.push_back(AngleRange{range.minAngle, minAngle});
            }
            if (range.maxAngle > maxAngle && range.maxAngle - maxAngle > kSmallestAngleRange) {
                m_scratch.push_back(AngleRange{maxAngle, range.maxAngle});
            }
        }
        m_ranges.swap(m_scratch);
    }

    std::vector<AngleRange> m_ranges;
    std::vector<AngleRange> m_scratch;
};

struct RiverGridEntry {
    int screenIndex = -1;
    float intensity = 0.0f;
};

// Port of Proland's ParticleGrid neighborhood grid. ScreenParticleLayer uses
// a particle radius of 4 * ScreenParticleLayer::radius and gridFactor 1, so
// each cell is approximately 4r; a particle is inserted in every cell covered
// by its screen-space bounding box. Cells keep at most 64 entries, dropping
// the lowest-intensity particle on overflow.
class RiverParticleGrid {
  public:
    static constexpr int kMaxEntriesPerCell = 64;

    void reset(uint16_t viewportWidth, uint16_t viewportHeight, float particleRadius) {
        m_particleRadius = std::max(4.0f * particleRadius, 1.0f);
        m_width = std::max(1, static_cast<int>(static_cast<float>(viewportWidth) / m_particleRadius));
        m_height = std::max(1, static_cast<int>(static_cast<float>(viewportHeight) / m_particleRadius));
        m_cellWidth = static_cast<float>(viewportWidth) / static_cast<float>(m_width);
        m_cellHeight = static_cast<float>(viewportHeight) / static_cast<float>(m_height);
        m_cells.assign(static_cast<std::size_t>(m_width) * m_height, {});
    }

    int cellX(float screenX) const {
        return std::clamp(static_cast<int>(std::floor(screenX / m_cellWidth)), 0, m_width - 1);
    }

    int cellY(float screenY) const {
        return std::clamp(static_cast<int>(std::floor(screenY / m_cellHeight)), 0, m_height - 1);
    }

    const std::vector<RiverGridEntry>& cell(float screenX, float screenY) const {
        return m_cells[static_cast<std::size_t>(cellY(screenY)) * m_width + cellX(screenX)];
    }

    void add(Vec2 screen, float intensity, int screenIndex) {
        const int minX = std::clamp(static_cast<int>(std::floor((screen.x - m_particleRadius) / m_cellWidth)), 0,
                                    m_width - 1);
        const int maxX = std::clamp(static_cast<int>(std::floor((screen.x + m_particleRadius) / m_cellWidth)), 0,
                                    m_width - 1);
        const int minY = std::clamp(static_cast<int>(std::floor((screen.y - m_particleRadius) / m_cellHeight)), 0,
                                    m_height - 1);
        const int maxY = std::clamp(static_cast<int>(std::floor((screen.y + m_particleRadius) / m_cellHeight)), 0,
                                    m_height - 1);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                std::vector<RiverGridEntry>& cell = m_cells[static_cast<std::size_t>(y) * m_width + x];
                if (static_cast<int>(cell.size()) < kMaxEntriesPerCell) {
                    cell.push_back(RiverGridEntry{screenIndex, intensity});
                    continue;
                }
                float minIntensity = intensity;
                int minEntry = 0;
                for (int i = 0; i < kMaxEntriesPerCell; ++i) {
                    if (cell[static_cast<std::size_t>(i)].intensity < minIntensity) {
                        minIntensity = cell[static_cast<std::size_t>(i)].intensity;
                        minEntry = i;
                    }
                }
                if (minIntensity < intensity) {
                    cell[static_cast<std::size_t>(minEntry)] = RiverGridEntry{screenIndex, intensity};
                }
            }
        }
    }

  private:
    float m_particleRadius = 1.0f;
    float m_cellWidth = 1.0f;
    float m_cellHeight = 1.0f;
    int m_width = 1;
    int m_height = 1;
    std::vector<std::vector<RiverGridEntry>> m_cells;
};

void findSpawnRanges(Vec2 origin, const std::vector<Vec2>& acceptedScreens,
                     const std::vector<RiverGridEntry>& neighbors, float radius, AngleRangeList& ranges) {
    ranges.reset();
    const float rangeSquared = 16.0f * radius * radius;
    for (const RiverGridEntry& neighbor : neighbors) {
        if (neighbor.screenIndex < 0 ||
            static_cast<std::size_t>(neighbor.screenIndex) >= acceptedScreens.size()) {
            continue;
        }
        const Vec2& screen = acceptedScreens[static_cast<std::size_t>(neighbor.screenIndex)];
        const Vec2 delta = subtract(screen, origin);
        const float distanceSquared = lengthSq(delta);
        if (distanceSquared <= 1.0e-8f || distanceSquared >= rangeSquared) {
            continue;
        }
        const float distance = std::sqrt(distanceSquared);
        const float angle = std::atan2(delta.y, delta.x);
        const float theta = std::acos(std::clamp(0.25f * distance / radius, -1.0f, 1.0f));
        ranges.subtract(angle - theta, angle + theta);
    }
}

uint32_t hashU32(uint32_t x);

int wrapIndex(int value, int period) {
    const int wrapped = value % period;
    return wrapped < 0 ? wrapped + period : wrapped;
}

uint32_t perlinHash(uint32_t seed) {
    uint32_t value = (seed ^ 12345391u) * 2654435769u;
    value ^= (value << 6) ^ (value >> 26);
    value *= 2654435769u;
    value += (value << 5) ^ (value >> 12);
    return value;
}

float perlinHashFloat(uint32_t seed, float low, float high) {
    return (high - low) * static_cast<float>(perlinHash(seed)) / 4294967295.0f + low;
}

float lerpFloat(float value0, float value1, float factor) {
    return (1.0f - factor) * value0 + factor * value1;
}

float trilerpFloat(float v000, float v100, float v010, float v110, float v001, float v101, float v011, float v111,
                   float fx, float fy, float fz) {
    const float low0 = lerpFloat(v000, v100, fx);
    const float high0 = lerpFloat(v010, v110, fx);
    const float low1 = lerpFloat(v001, v101, fx);
    const float high1 = lerpFloat(v011, v111, fx);
    return lerpFloat(lerpFloat(low0, high0, fy), lerpFloat(low1, high1, fy), fz);
}

class RiverPerlinNoise {
  public:
    explicit RiverPerlinNoise(uint32_t seed = 171717u) {
        for (int i = 0; i < kTableSize; ++i) {
            Vec3 direction;
            float lengthSquared = 0.0f;
            do {
                direction = Vec3(perlinHashFloat(seed++, -1.0f, 1.0f), perlinHashFloat(seed++, -1.0f, 1.0f),
                                 perlinHashFloat(seed++, -1.0f, 1.0f));
                lengthSquared = dot(direction, direction);
            } while (lengthSquared > 1.0f || lengthSquared == 0.0f);
            m_basis[i] = direction * (1.0f / std::sqrt(lengthSquared));
            m_permutation[i] = i;
        }
        for (int i = 1; i < kTableSize; ++i) {
            const int j = static_cast<int>(perlinHash(seed++) % static_cast<uint32_t>(i + 1));
            std::swap(m_permutation[i], m_permutation[j]);
        }
    }

    float operator()(float x, float y, float z, int periodXY, int periodZ) const {
        const float floorX = std::floor(x);
        const float floorY = std::floor(y);
        const float floorZ = std::floor(z);
        const int i = static_cast<int>(floorX);
        const int j = static_cast<int>(floorY);
        const int k = static_cast<int>(floorZ);
        const Vec3& n000 = m_basis[hashIndex(i, j, k, periodXY, periodZ)];
        const Vec3& n100 = m_basis[hashIndex(i + 1, j, k, periodXY, periodZ)];
        const Vec3& n010 = m_basis[hashIndex(i, j + 1, k, periodXY, periodZ)];
        const Vec3& n110 = m_basis[hashIndex(i + 1, j + 1, k, periodXY, periodZ)];
        const Vec3& n001 = m_basis[hashIndex(i, j, k + 1, periodXY, periodZ)];
        const Vec3& n101 = m_basis[hashIndex(i + 1, j, k + 1, periodXY, periodZ)];
        const Vec3& n011 = m_basis[hashIndex(i, j + 1, k + 1, periodXY, periodZ)];
        const Vec3& n111 = m_basis[hashIndex(i + 1, j + 1, k + 1, periodXY, periodZ)];
        const float fx = x - floorX;
        const float fy = y - floorY;
        const float fz = z - floorZ;
        const float sx = fx * fx * fx * (10.0f - fx * (15.0f - fx * 6.0f));
        const float sy = fy * fy * fy * (10.0f - fy * (15.0f - fy * 6.0f));
        const float sz = fz * fz * fz * (10.0f - fz * (15.0f - fz * 6.0f));
        return trilerpFloat(fx * n000.x + fy * n000.y + fz * n000.z,
                            (fx - 1.0f) * n100.x + fy * n100.y + fz * n100.z,
                            fx * n010.x + (fy - 1.0f) * n010.y + fz * n010.z,
                            (fx - 1.0f) * n110.x + (fy - 1.0f) * n110.y + fz * n110.z,
                            fx * n001.x + fy * n001.y + (fz - 1.0f) * n001.z,
                            (fx - 1.0f) * n101.x + fy * n101.y + (fz - 1.0f) * n101.z,
                            fx * n011.x + (fy - 1.0f) * n011.y + (fz - 1.0f) * n011.z,
                            (fx - 1.0f) * n111.x + (fy - 1.0f) * n111.y + (fz - 1.0f) * n111.z, sx, sy, sz);
    }

  private:
    static constexpr int kTableSize = 256;

    uint32_t hashIndex(int i, int j, int k, int periodXY, int periodZ) const {
        return static_cast<uint32_t>(
            m_permutation[(m_permutation[(m_permutation[i % periodXY] + j) % periodXY] + k % periodZ) % periodXY]);
    }

    Vec3 m_basis[kTableSize];
    int m_permutation[kTableSize];
};

float wrap01(float value) {
    return value - std::floor(value);
}

Vec4 multiply(const Mat4& matrix, const Vec4& value) {
    return Vec4(matrix.values[0] * value.x + matrix.values[4] * value.y + matrix.values[8] * value.z +
                    matrix.values[12] * value.w,
                matrix.values[1] * value.x + matrix.values[5] * value.y + matrix.values[9] * value.z +
                    matrix.values[13] * value.w,
                matrix.values[2] * value.x + matrix.values[6] * value.y + matrix.values[10] * value.z +
                    matrix.values[14] * value.w,
                matrix.values[3] * value.x + matrix.values[7] * value.y + matrix.values[11] * value.z +
                    matrix.values[15] * value.w);
}

uint32_t hashU32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

RiverGraph simplifyGraph(const RiverGraph& graph, std::size_t maxPoints) {
    if (graph.axis.size() <= maxPoints || maxPoints < 2) {
        return graph;
    }
    RiverGraph simplified;
    simplified.width = graph.width;
    const std::size_t axisCount = graph.axis.size();
    const std::size_t step = std::max<std::size_t>(1, (axisCount + maxPoints - 1) / maxPoints);
    const auto append = [](const std::vector<Vec2>& source, std::size_t stride, std::size_t count,
                           std::vector<Vec2>& destination) {
        destination.clear();
        for (std::size_t index = 0; index < source.size(); index += stride) {
            destination.push_back(source[index]);
        }
        if (source.empty() || destination.empty() || destination.back() != source.back()) {
            destination.push_back(source.back());
        }
        (void)count;
    };
    append(graph.axis, step, maxPoints, simplified.axis);
    simplified.leftBank.potential = graph.leftBank.potential;
    simplified.leftBank.width = graph.leftBank.width;
    simplified.rightBank.potential = graph.rightBank.potential;
    simplified.rightBank.width = graph.rightBank.width;
    append(graph.leftBank.points, step, maxPoints, simplified.leftBank.points);
    append(graph.rightBank.points, step, maxPoints, simplified.rightBank.points);
    return simplified;
}

float pointSegmentDistanceSq(Vec2 point, Vec2 start, Vec2 end, float& tOut) {
    const Vec2 segment = subtract(end, start);
    const float lengthSquared = dot(segment, segment);
    if (lengthSquared <= 1.0e-12f) {
        tOut = 0.0f;
        return lengthSq(subtract(point, start));
    }
    const float t = std::clamp(dot(subtract(point, start), segment) / lengthSquared, 0.0f, 1.0f);
    tOut = t;
    return lengthSq(subtract(point, add(start, scale(segment, t))));
}

} // namespace

RiverFlowTransition transitionRiverFlow(RiverFlowStatus status, bool firstVelocityQuery, Vec2 oldVelocity,
                                        bool sampleInside, Vec2 sampleVelocity, bool insideNeighbor) {
    RiverFlowTransition transition;
    transition.status = status;
    transition.velocity = oldVelocity;
    if (status == RiverFlowStatus::Inside || status == RiverFlowStatus::Unknown) {
        if (sampleInside) {
            transition.status = RiverFlowStatus::Inside;
            transition.velocity = sampleVelocity;
        } else if (firstVelocityQuery) {
            transition.status = RiverFlowStatus::Outside;
            transition.velocity = Vec2(0.0f, 0.0f);
            if (insideNeighbor) {
                transition.status = RiverFlowStatus::Near;
                transition.kill = true;
            }
        } else {
            transition.status = RiverFlowStatus::Leaving;
        }
    } else if (status == RiverFlowStatus::Leaving) {
        if (sampleInside) {
            transition.status = RiverFlowStatus::Inside;
            transition.velocity = sampleVelocity;
        } else if (insideNeighbor) {
            transition.status = RiverFlowStatus::Leaving;
        } else {
            transition.status = RiverFlowStatus::Outside;
            transition.velocity = Vec2(0.0f, 0.0f);
        }
    } else if (status == RiverFlowStatus::Outside) {
        transition.velocity = Vec2(0.0f, 0.0f);
        if (insideNeighbor) {
            transition.status = RiverFlowStatus::Near;
            transition.kill = true;
        }
    }
    return transition;
}

float riverLifecycleFade(float age, float fadeIn, float active, float fadeOut) {
    const float clampedAge = std::max(age, 0.0f);
    const float fadeInEnd = std::max(fadeIn, 0.0f);
    const float fadeOutStart = fadeInEnd + std::max(active, 0.0f);
    if (fadeInEnd > 0.0f && clampedAge < fadeInEnd) {
        return std::clamp(clampedAge / fadeInEnd, 0.0f, 1.0f);
    }
    if (clampedAge < fadeOutStart) {
        return 1.0f;
    }
    if (fadeOut <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(1.0f - (clampedAge - fadeOutStart) / fadeOut, 0.0f, 1.0f);
}

bool RiverParticleSystem::init(const RiverGraph& graph, const RiverMeshSettings& mesh,
                               const RiverParticleSettings& settings) {
    m_graph = simplifyGraph(graph, 65);
    m_mesh = mesh;
    m_settings = settings;
    m_settings.textureSize = std::clamp<uint32_t>(m_settings.textureSize, 32, 1024);
    m_settings.waveTileSize = std::clamp<uint32_t>(m_settings.waveTileSize, 8, 256);
    m_settings.waveTimeLoop = std::clamp<uint32_t>(m_settings.waveTimeLoop, 1, 16);
    m_settings.waveTimeStep = std::max(m_settings.waveTimeStep, 0.01f);
    m_settings.particleRadius = std::max(m_settings.particleRadius, 2.0f);
    m_settings.maxParticles = std::max<uint32_t>(m_settings.maxParticles, 16);
    m_settings.fadeInSeconds = std::max(m_settings.fadeInSeconds, 0.01f);
    m_settings.activeSeconds = std::max(m_settings.activeSeconds, 0.0f);
    m_settings.fadeOutSeconds = std::max(m_settings.fadeOutSeconds, 0.01f);
    if (m_settings.randomOffsetMin.x > m_settings.randomOffsetMax.x) {
        std::swap(m_settings.randomOffsetMin.x, m_settings.randomOffsetMax.x);
    }
    if (m_settings.randomOffsetMin.y > m_settings.randomOffsetMax.y) {
        std::swap(m_settings.randomOffsetMin.y, m_settings.randomOffsetMax.y);
    }
    if (m_settings.randomOffsetMin.z > m_settings.randomOffsetMax.z) {
        std::swap(m_settings.randomOffsetMin.z, m_settings.randomOffsetMax.z);
    }
    m_settings.speedScale = std::max(m_settings.speedScale, 0.0f);
    m_settings.waveSlope = std::max(m_settings.waveSlope, 0.0f);
    m_settings.waveSlopeFactor = std::clamp(m_settings.waveSlopeFactor, 0.0f, 32.0f);
    m_settings.waveLength = std::max(m_settings.waveLength, 1.0f);
    m_settings.spawnCellFactor = std::max(m_settings.spawnCellFactor, 1.0f);

    m_flow = HydroFlow(m_graph);
    m_particles.clear();
    clearDepthSnapshot();
    m_depthQueries.clear();
    m_pendingDepthResults.clear();
    m_lastDepthViewProj = Mat4{};
    m_pendingDepthViewProj = Mat4{};
    m_lastDepthWidth = 0;
    m_lastDepthHeight = 0;
    m_pendingDepthWidth = 0;
    m_pendingDepthHeight = 0;
    m_nextParticleId = 1;
    m_nearKillCount = 0;
    m_depthFlowPromotions = 0;
    m_depthRequestMode = RiverDepthRequestMode::None;
    m_depthPackAppliedLogged = false;
    m_depthFlowPromotedLogged = false;
    m_hasLastDepthView = false;
    m_hasPendingDepthResults = false;
    m_gpuParticleParams.assign(static_cast<std::size_t>(m_settings.maxParticles) * 8u, 0.0f);
    m_gpuSpriteTable.clear();
    m_gpuGridSpans.clear();
    m_gpuParticleCount = 0;
    m_gpuGridWidth = 0;
    m_gpuGridHeight = 0;
    m_gpuGridCellWidth = 0.0f;
    m_gpuGridCellHeight = 0.0f;
    m_gpuGridCellSize = 0.0f;
    m_randomState = 0x9e3779b9u;
    m_valid = m_graph.axis.size() >= 2 && m_graph.width > 0.0f;
    if (!m_valid) {
        m_waveSlopeFrames.clear();
        m_waveHeightFrames.clear();
        m_baseNormalFrames.clear();
        m_baseColorPixels.clear();
        m_baseNormalPixels.clear();
        m_baseNormalMips.clear();
        m_splatNormalMips.clear();
        m_normalPixels.clear();
        m_waveFrameCount = 1;
        m_currentWaveFrame = 0;
        m_gpuParticleParams.clear();
        m_gpuSpriteTable.clear();
        return false;
    }

    buildTextures();
    return !m_baseColorPixels.empty() && !m_baseNormalPixels.empty();
}

bool RiverParticleSystem::insideRiver(Vec2 position) const {
    return m_flow.inside(position);
}

float RiverParticleSystem::random01() const {
    m_randomState = hashU32(m_randomState + 0x9e3779b9u);
    return static_cast<float>(m_randomState & 0x00ffffffu) / 16777216.0f;
}

uint32_t RiverParticleSystem::worldParticleCount() const {
    return static_cast<uint32_t>(std::count_if(
        m_particles.begin(), m_particles.end(), [](const RiverParticle& particle) { return particle.worldValid; }));
}

uint32_t RiverParticleSystem::depthWorldParticleCount() const {
    return static_cast<uint32_t>(std::count_if(m_particles.begin(), m_particles.end(),
                                               [](const RiverParticle& particle) {
                                                   return particle.worldValid || particle.depthWorldValid;
                                               }));
}

uint32_t RiverParticleSystem::leavingParticleCount() const {
    return static_cast<uint32_t>(std::count_if(
        m_particles.begin(), m_particles.end(), [](const RiverParticle& particle) {
            return particle.worldValid && particle.flowStatus == RiverFlowStatus::Leaving;
        }));
}

uint32_t RiverParticleSystem::nearParticleCount() const {
    return static_cast<uint32_t>(std::count_if(
        m_particles.begin(), m_particles.end(), [](const RiverParticle& particle) {
            return (particle.worldValid || particle.depthWorldValid) &&
                   particle.flowStatus == RiverFlowStatus::Near;
        }));
}

uint32_t RiverParticleSystem::outsideParticleCount() const {
    return static_cast<uint32_t>(std::count_if(
        m_particles.begin(), m_particles.end(), [](const RiverParticle& particle) {
            return (particle.worldValid || particle.depthWorldValid) &&
                   particle.flowStatus == RiverFlowStatus::Outside;
        }));
}

void RiverParticleSystem::setDepthSnapshot(std::vector<float> depths, uint16_t width, uint16_t height,
                                           const Mat4& viewProj) {
    const std::size_t expected = static_cast<std::size_t>(width) * height;
    if (width == 0 || height == 0 || depths.size() < expected) {
        clearDepthSnapshot();
        return;
    }
    m_depthDepths = std::move(depths);
    m_depthDepths.resize(expected);
    m_depthWidth = width;
    m_depthHeight = height;
    m_depthViewProj = viewProj;
    m_depthInvViewProj = inverse(viewProj);
    m_depthBackfillLogged = false;
}

void RiverParticleSystem::clearDepthSnapshot() {
    m_depthDepths.clear();
    m_depthWidth = 0;
    m_depthHeight = 0;
    m_depthViewProj = Mat4{};
    m_depthInvViewProj = Mat4{};
}

bool RiverParticleSystem::depthSnapshotMatches(const Mat4& viewProj) const {
    return !m_depthDepths.empty() &&
           std::memcmp(m_depthViewProj.values.data(), viewProj.values.data(), sizeof(float) * 16) == 0;
}

bool RiverParticleSystem::depthSnapshotMatches(const Mat4& viewProj, uint16_t width, uint16_t height) const {
    return depthSnapshotMatches(viewProj) && m_depthWidth == width && m_depthHeight == height;
}

bool RiverParticleSystem::depthToWorld(float screenX, float screenY, float depth, const Mat4& invViewProj,
                                       uint16_t width, uint16_t height, Vec3& worldPosition) const {
    if (!std::isfinite(depth) || depth < 0.0f || depth >= 0.999999f || width == 0 || height == 0) {
        return false;
    }
    const float ndcX = 2.0f * screenX / static_cast<float>(width) - 1.0f;
    const float ndcY = 1.0f - 2.0f * screenY / static_cast<float>(height);
    const Vec4 clip = multiply(invViewProj, Vec4(ndcX, ndcY, depth * 2.0f - 1.0f, 1.0f));
    if (clip.w <= 1.0e-6f) {
        return false;
    }
    worldPosition = Vec3(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
    return std::isfinite(worldPosition.x) && std::isfinite(worldPosition.y) && std::isfinite(worldPosition.z);
}

bool RiverParticleSystem::screenToDepthWorld(float screenX, float screenY, Vec3& worldPosition) const {
    if (m_depthDepths.empty() || m_depthWidth == 0 || m_depthHeight == 0) {
        return false;
    }
    const int pixelX = std::clamp(static_cast<int>(std::floor(screenX)), 0, m_depthWidth - 1);
    const int pixelY = std::clamp(static_cast<int>(std::floor(screenY)), 0, m_depthHeight - 1);
    const std::size_t row = static_cast<std::size_t>(m_depthHeight - 1 - pixelY);
    const float depth = m_depthDepths[row * m_depthWidth + static_cast<std::size_t>(pixelX)];
    return depthToWorld(screenX, screenY, depth, m_depthInvViewProj, m_depthWidth, m_depthHeight, worldPosition);
}

std::vector<RiverDepthQuery> RiverParticleSystem::takeDepthQueries() {
    std::vector<RiverDepthQuery> queries = std::move(m_depthQueries);
    m_depthQueries.clear();
    return queries;
}

void RiverParticleSystem::submitParticleDepths(std::vector<float> depths, std::vector<RiverDepthQuery> queries,
                                               const Mat4& viewProj, uint16_t width, uint16_t height) {
    if (queries.empty() || depths.size() < queries.size() || width == 0 || height == 0) {
        return;
    }
    m_pendingDepthResults.clear();
    m_pendingDepthResults.reserve(queries.size());
    for (std::size_t index = 0; index < queries.size(); ++index) {
        PendingDepthResult result;
        result.particleId = queries[index].particleId;
        result.screen = queries[index].screen;
        result.depth = depths[index];
        m_pendingDepthResults.push_back(result);
    }
    m_pendingDepthViewProj = viewProj;
    m_pendingDepthWidth = width;
    m_pendingDepthHeight = height;
    m_hasPendingDepthResults = true;
}

void RiverParticleSystem::applyPendingDepthResults() {
    if (!m_hasPendingDepthResults) {
        return;
    }
    const Mat4 invViewProj = inverse(m_pendingDepthViewProj);
    std::unordered_map<uint32_t, std::size_t> particleIndices;
    particleIndices.reserve(m_particles.size());
    for (std::size_t index = 0; index < m_particles.size(); ++index) {
        particleIndices.emplace(m_particles[index].id, index);
    }
    uint32_t applied = 0;
    for (const PendingDepthResult& result : m_pendingDepthResults) {
        const auto found = particleIndices.find(result.particleId);
        if (found == particleIndices.end()) {
            continue;
        }
        RiverParticle& particle = m_particles[found->second];
        if (particle.worldValid || particle.depthWorldValid) {
            continue;
        }
        Vec3 world;
        if (!depthToWorld(result.screen.x, result.screen.y, result.depth, invViewProj, m_pendingDepthWidth,
                          m_pendingDepthHeight, world)) {
            continue;
        }
        particle.world = Vec2(world.x, world.z);
        particle.worldHeight = world.y;
        particle.depthWorldValid = true;
        ++applied;
    }
    if (!m_depthPackAppliedLogged) {
        HBG_LOGI("river depth pack applied: queries={} applied={} active={} river={} depth={}",
                 m_pendingDepthResults.size(), applied, m_particles.size(), worldParticleCount(),
                 depthWorldParticleCount());
        m_depthPackAppliedLogged = true;
    }
    m_pendingDepthResults.clear();
    m_pendingDepthViewProj = Mat4{};
    m_pendingDepthWidth = 0;
    m_pendingDepthHeight = 0;
    m_hasPendingDepthResults = false;
}

uint32_t RiverParticleSystem::fadingParticleCount() const {
    return static_cast<uint32_t>(std::count_if(
        m_particles.begin(), m_particles.end(), [this](const RiverParticle& particle) { return isFading(particle); }));
}

void RiverParticleSystem::buildWaveFrames() {
    const uint32_t size = m_settings.waveTileSize;
    const uint32_t frameCount = std::clamp<uint32_t>(m_settings.waveTimeLoop, 1, 16);
    m_waveFrameCount = frameCount;
    m_waveSlopeFrames.assign(frameCount, {});
    m_waveHeightFrames.assign(frameCount, {});
    if (size < 4) {
        return;
    }

    const RiverPerlinNoise noise(171717u);
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        std::vector<float>& slopes = m_waveSlopeFrames[frame];
        slopes.assign(static_cast<std::size_t>(size) * size * 2, 0.0f);
        std::vector<float>& heights = m_waveHeightFrames[frame];
        heights.assign(static_cast<std::size_t>(size) * size, 0.0f);
        const float phase = frameCount > 1 ? static_cast<float>(frame) / static_cast<float>(frameCount) * 4.0f : 0.0f;
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                float height = 0.0f;
                int frequency = 32;
                float amplitude = 1.0f;
                for (int octave = 0; octave < 4; ++octave) {
                    height += noise(u * static_cast<float>(frequency), v * static_cast<float>(frequency), phase,
                                    frequency, 4) *
                              amplitude;
                    frequency *= 2;
                    amplitude *= 0.5f;
                }
                heights[static_cast<std::size_t>(y) * size + x] = height;
            }
        }

        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size; ++x) {
                const int x0 = wrapIndex(static_cast<int>(x) - 1, static_cast<int>(size));
                const int x1 = wrapIndex(static_cast<int>(x) + 1, static_cast<int>(size));
                const int y0 = wrapIndex(static_cast<int>(y) - 1, static_cast<int>(size));
                const int y1 = wrapIndex(static_cast<int>(y) + 1, static_cast<int>(size));
                const std::size_t row = static_cast<std::size_t>(y) * size;
                const float dx =
                    heights[row + static_cast<std::size_t>(x1)] - heights[row + static_cast<std::size_t>(x0)];
                const float dy = heights[static_cast<std::size_t>(y1) * size + x] -
                                 heights[static_cast<std::size_t>(y0) * size + x];
                const std::size_t index = (row + x) * 2;
                slopes[index] = dx * kRiverWaveSlopeScale;
                slopes[index + 1] = dy * kRiverWaveSlopeScale;
            }
        }
    }
}

RiverParticleSystem::WaveSample RiverParticleSystem::sampleWaveTile(Vec2 uv, uint32_t frame) const {
    const uint32_t size = m_settings.waveTileSize;
    if (size == 0 || frame >= m_waveSlopeFrames.size() || m_waveSlopeFrames[frame].empty()) {
        return {};
    }
    const std::vector<float>& slopes = m_waveSlopeFrames[frame];
    const float x = wrap01(uv.x) * static_cast<float>(size) - 0.5f;
    const float y = wrap01(uv.y) * static_cast<float>(size) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto wrapIndex = [size](int value) {
        const int wrapped = value % static_cast<int>(size);
        return wrapped < 0 ? wrapped + static_cast<int>(size) : wrapped;
    };
    const int x1 = wrapIndex(x0 + 1);
    const int y1 = wrapIndex(y0 + 1);
    const int ix0 = wrapIndex(x0);
    const int iy0 = wrapIndex(y0);
    const auto at = [&slopes, size](int xIndex, int yIndex) {
        const std::size_t index = (static_cast<std::size_t>(yIndex) * size + static_cast<std::size_t>(xIndex)) * 2;
        return Vec2(slopes[index], slopes[index + 1]);
    };
    const Vec2 low = add(scale(at(ix0, iy0), 1.0f - tx), scale(at(x1, iy0), tx));
    const Vec2 high = add(scale(at(ix0, y1), 1.0f - tx), scale(at(x1, y1), tx));
    return WaveSample{add(scale(low, 1.0f - ty), scale(high, ty))};
}

float RiverParticleSystem::sampleWaveHeight(Vec2 uv, uint32_t frame) const {
    const uint32_t size = m_settings.waveTileSize;
    if (size == 0 || frame >= m_waveHeightFrames.size() || m_waveHeightFrames[frame].empty()) {
        return 0.0f;
    }
    const std::vector<float>& heights = m_waveHeightFrames[frame];
    const float x = wrap01(uv.x) * static_cast<float>(size) - 0.5f;
    const float y = wrap01(uv.y) * static_cast<float>(size) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const int ix0 = wrapIndex(x0, static_cast<int>(size));
    const int iy0 = wrapIndex(y0, static_cast<int>(size));
    const int ix1 = wrapIndex(x0 + 1, static_cast<int>(size));
    const int iy1 = wrapIndex(y0 + 1, static_cast<int>(size));
    const auto at = [&heights, size](int xIndex, int yIndex) {
        return heights[static_cast<std::size_t>(yIndex) * size + static_cast<std::size_t>(xIndex)];
    };
    const float low = at(ix0, iy0) * (1.0f - tx) + at(ix1, iy0) * tx;
    const float high = at(ix0, iy1) * (1.0f - tx) + at(ix1, iy1) * tx;
    return low * (1.0f - ty) + high * ty;
}

void RiverParticleSystem::buildBaseHeightField(uint32_t size, uint32_t frame, std::vector<float>& heights) const {
    heights.assign(static_cast<std::size_t>(size) * size, 0.0f);
    if (size == 0) {
        return;
    }
    const float step = 2.0f / static_cast<float>(size);
    for (uint32_t y = 0; y < size; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) * step;
        for (uint32_t x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * step;
            heights[static_cast<std::size_t>(y) * size + x] = sampleWaveHeight(Vec2(u, v), frame);
        }
    }
}

void RiverParticleSystem::encodeBaseNormalLevel(const std::vector<float>& heights, uint32_t size,
                                                std::vector<float>& pixels) const {
    pixels.assign(static_cast<std::size_t>(size) * size * 4, 1.0f);
    if (size == 0 || heights.size() != static_cast<std::size_t>(size) * size) {
        return;
    }
    for (uint32_t y = 0; y < size; ++y) {
        const uint32_t y0 = static_cast<uint32_t>(wrapIndex(static_cast<int>(y) - 1, static_cast<int>(size)));
        const uint32_t y1 = static_cast<uint32_t>(wrapIndex(static_cast<int>(y) + 1, static_cast<int>(size)));
        for (uint32_t x = 0; x < size; ++x) {
            const uint32_t x0 = static_cast<uint32_t>(wrapIndex(static_cast<int>(x) - 1, static_cast<int>(size)));
            const uint32_t x1 = static_cast<uint32_t>(wrapIndex(static_cast<int>(x) + 1, static_cast<int>(size)));
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            const float bank = smoothStep(0.02f, 0.16f, u) * smoothStep(0.02f, 0.16f, 1.0f - u);
            const float damper = 0.18f + 0.82f * bank;
            const float dx = heights[static_cast<std::size_t>(y) * size + x1] -
                             heights[static_cast<std::size_t>(y) * size + x0];
            const float dy = heights[static_cast<std::size_t>(y1) * size + x] -
                             heights[static_cast<std::size_t>(y0) * size + x];
            const float slopeX = dx * kRiverWaveSlopeScale * m_settings.waveSlope * damper;
            const float slopeY = dy * kRiverWaveSlopeScale * m_settings.waveSlope * damper;
            const std::size_t index = (static_cast<std::size_t>(y) * size + x) * 4;
            pixels[index] = slopeX;
            pixels[index + 1] = slopeY;
            pixels[index + 2] = 1.0f;
            pixels[index + 3] = bank;
        }
    }
}

void RiverParticleSystem::buildNormalMipChain(const std::vector<float>& source, uint32_t size,
                                              std::vector<std::vector<float>>& mips) const {
    mips.clear();
    if (source.empty() || size == 0) {
        return;
    }
    mips.push_back(source);
    uint32_t mipSize = size;
    while (mipSize > 1) {
        const uint32_t nextSize = std::max(1u, mipSize / 2);
        const std::vector<float>& level = mips.back();
        std::vector<float> target(static_cast<std::size_t>(nextSize) * nextSize * 4, 0.0f);
        for (uint32_t y = 0; y < nextSize; ++y) {
            const uint32_t y0 = std::min(y * 2u, mipSize - 1u);
            const uint32_t y1 = std::min(y * 2u + 1u, mipSize - 1u);
            for (uint32_t x = 0; x < nextSize; ++x) {
                const uint32_t x0 = std::min(x * 2u, mipSize - 1u);
                const uint32_t x1 = std::min(x * 2u + 1u, mipSize - 1u);
                const std::size_t index00 = (static_cast<std::size_t>(y0) * mipSize + x0) * 4;
                const std::size_t index10 = (static_cast<std::size_t>(y0) * mipSize + x1) * 4;
                const std::size_t index01 = (static_cast<std::size_t>(y1) * mipSize + x0) * 4;
                const std::size_t index11 = (static_cast<std::size_t>(y1) * mipSize + x1) * 4;
                const std::size_t targetIndex = (static_cast<std::size_t>(y) * nextSize + x) * 4;
                for (uint32_t channel = 0; channel < 4; ++channel) {
                    const float sum = level[index00 + channel] + level[index10 + channel] +
                                      level[index01 + channel] + level[index11 + channel];
                    target[targetIndex + channel] = sum * 0.25f;
                }
            }
        }
        mips.push_back(std::move(target));
        mipSize = nextSize;
    }
}

void RiverParticleSystem::buildBaseColorPixels() {
    const uint32_t size = m_settings.textureSize;
    m_baseColorPixels.assign(static_cast<std::size_t>(size) * size * 4, 255);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
            const float bank = smoothStep(0.02f, 0.16f, u) * smoothStep(0.02f, 0.16f, 1.0f - u);
            const float depth = smoothStep(0.10f, 0.85f, bank);
            const float grain = 0.94f + 0.06f * std::sin(u * 41.0f + v * 73.0f) * std::sin(u * 19.0f - v * 29.0f);
            const Vec3 shallow(0.19f, 0.31f, 0.25f);
            const Vec3 deep(0.012f, 0.055f, 0.085f);
            const Vec3 color(shallow.x * (1.0f - depth) + deep.x * depth, shallow.y * (1.0f - depth) + deep.y * depth,
                             shallow.z * (1.0f - depth) + deep.z * depth);
            const std::size_t index = (static_cast<std::size_t>(y) * size + x) * 4;
            m_baseColorPixels[index] = static_cast<uint8_t>(std::clamp(color.x * grain, 0.0f, 1.0f) * 255.0f + 0.5f);
            m_baseColorPixels[index + 1] =
                static_cast<uint8_t>(std::clamp(color.y * grain, 0.0f, 1.0f) * 255.0f + 0.5f);
            m_baseColorPixels[index + 2] =
                static_cast<uint8_t>(std::clamp(color.z * grain, 0.0f, 1.0f) * 255.0f + 0.5f);
            m_baseColorPixels[index + 3] = static_cast<uint8_t>(std::clamp(depth, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
}

void RiverParticleSystem::buildBaseNormalFrame(uint32_t frame) {
    const uint32_t size = m_settings.textureSize;
    std::vector<float> heights;
    buildBaseHeightField(size, frame, heights);
    encodeBaseNormalLevel(heights, size, m_baseNormalFrames[frame]);
}

void RiverParticleSystem::buildBaseNormalMips() {
    m_baseNormalMips.clear();
    if (m_baseNormalPixels.empty()) {
        return;
    }

    const uint32_t size = m_settings.textureSize;
    if (size < 4) {
        return;
    }

    m_baseNormalMips.push_back(m_baseNormalPixels);
    if (!m_waveHeightFrames.empty() && m_currentWaveFrame < m_waveHeightFrames.size()) {
        std::vector<float> heights;
        buildBaseHeightField(size, m_currentWaveFrame, heights);
        uint32_t mipSize = size;
        while (mipSize > 1) {
            const uint32_t nextSize = std::max(1u, mipSize / 2);
            std::vector<float> target(static_cast<std::size_t>(nextSize) * nextSize, 0.0f);
            for (uint32_t y = 0; y < nextSize; ++y) {
                const uint32_t y0 = std::min(y * 2u, mipSize - 1u);
                const uint32_t y1 = std::min(y * 2u + 1u, mipSize - 1u);
                for (uint32_t x = 0; x < nextSize; ++x) {
                    const uint32_t x0 = std::min(x * 2u, mipSize - 1u);
                    const uint32_t x1 = std::min(x * 2u + 1u, mipSize - 1u);
                    const float sum = heights[static_cast<std::size_t>(y0) * mipSize + x0] +
                                      heights[static_cast<std::size_t>(y0) * mipSize + x1] +
                                      heights[static_cast<std::size_t>(y1) * mipSize + x0] +
                                      heights[static_cast<std::size_t>(y1) * mipSize + x1];
                    target[static_cast<std::size_t>(y) * nextSize + x] = sum * 0.25f;
                }
            }
            std::vector<float> pixels;
            encodeBaseNormalLevel(target, nextSize, pixels);
            m_baseNormalMips.push_back(std::move(pixels));
            heights = std::move(target);
            mipSize = nextSize;
        }
        return;
    }

    buildNormalMipChain(m_baseNormalPixels, size, m_baseNormalMips);
}

void RiverParticleSystem::buildTextures() {
    buildWaveFrames();
    buildBaseColorPixels();
    m_baseNormalFrames.assign(m_waveFrameCount, {});
    for (uint32_t frame = 0; frame < m_waveFrameCount; ++frame) {
        buildBaseNormalFrame(frame);
    }
    m_currentWaveFrame = 0;
    m_baseNormalPixels = m_baseNormalFrames.front();
    buildBaseNormalMips();
    m_normalPixels = m_baseNormalPixels;
    m_splatNormalMips = m_baseNormalMips;
}

bool RiverParticleSystem::worldToRiverUv(Vec2 worldPosition, Vec2& uv) const {
    if (m_graph.axis.size() < 2 || m_graph.width <= 0.0f) {
        return false;
    }
    float bestDistance = std::numeric_limits<float>::max();
    float bestLateral = 0.0f;
    for (std::size_t index = 1; index < m_graph.axis.size(); ++index) {
        const Vec2 start = m_graph.axis[index - 1];
        const Vec2 end = m_graph.axis[index];
        float t = 0.0f;
        const float distance = pointSegmentDistanceSq(worldPosition, start, end, t);
        if (distance >= bestDistance) {
            continue;
        }
        bestDistance = distance;
        const Vec2 segment = subtract(end, start);
        const Vec2 tangent = normalize(segment);
        const Vec2 normal(-tangent.y, tangent.x);
        const Vec2 closest = add(start, scale(segment, t));
        bestLateral = dot(subtract(worldPosition, closest), normal);
    }
    const float u = 0.5f + bestLateral / m_graph.width;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const float firstY = m_graph.axis.front().y;
    const float v = (worldPosition.y - firstY) / std::max(m_mesh.uvLength, 1.0e-3f);
    uv = Vec2(u, v);
    return true;
}

bool RiverParticleSystem::projectToScreen(const Vec3& world, const Mat4& viewProj, uint16_t viewportWidth,
                                          uint16_t viewportHeight, Vec2& screen) const {
    if (viewportWidth == 0 || viewportHeight == 0) {
        return false;
    }
    const Vec4 clip = multiply(viewProj, Vec4(world.x, world.y, world.z, 1.0f));
    if (clip.w <= 1.0e-6f) {
        return false;
    }
    const Vec2 ndc(clip.x / clip.w, clip.y / clip.w);
    if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y)) {
        return false;
    }
    screen.x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(viewportWidth);
    screen.y = (0.5f - ndc.y * 0.5f) * static_cast<float>(viewportHeight);
    return true;
}

bool RiverParticleSystem::screenToWaterPlane(float screenX, float screenY, const Mat4& invViewProj,
                                             const Vec3& cameraPosition, uint16_t viewportWidth,
                                             uint16_t viewportHeight, Vec2& waterPosition) const {
    if (viewportWidth == 0 || viewportHeight == 0) {
        return false;
    }
    const float ndcX = 2.0f * screenX / static_cast<float>(viewportWidth) - 1.0f;
    const float ndcY = 1.0f - 2.0f * screenY / static_cast<float>(viewportHeight);
    const Vec4 farPoint = multiply(invViewProj, Vec4(ndcX, ndcY, 1.0f, 1.0f));
    if (farPoint.w <= 1.0e-6f) {
        return false;
    }
    const Vec3 farPosition(farPoint.x / farPoint.w, farPoint.y / farPoint.w, farPoint.z / farPoint.w);
    const Vec3 direction = normalize(farPosition - cameraPosition);
    if (std::abs(direction.y) <= 1.0e-5f) {
        return false;
    }
    const float t = (m_mesh.baseHeight - cameraPosition.y) / direction.y;
    if (t <= 0.0f) {
        return false;
    }
    waterPosition = Vec2(cameraPosition.x + direction.x * t, cameraPosition.z + direction.z * t);
    return true;
}

bool RiverParticleSystem::screenToRiver(float screenX, float screenY, const Mat4& invViewProj,
                                        const Vec3& cameraPosition, uint16_t viewportWidth, uint16_t viewportHeight,
                                        Vec2& riverPosition) const {
    Vec2 waterPosition;
    if (!screenToWaterPlane(screenX, screenY, invViewProj, cameraPosition, viewportWidth, viewportHeight,
                            waterPosition) ||
        !insideRiver(waterPosition)) {
        return false;
    }
    riverPosition = waterPosition;
    return true;
}

void RiverParticleSystem::spawnParticle(Vec2 worldPosition, Vec2 screenPosition, bool riverValid,
                                        bool depthWorldValid, float worldHeight) {
    RiverParticle particle;
    particle.id = m_nextParticleId++;
    particle.world = worldPosition;
    particle.screen = screenPosition;
    particle.worldHeight = riverValid ? m_mesh.baseHeight : worldHeight;
    particle.worldValid = riverValid;
    particle.depthWorldValid = depthWorldValid;
    // TerrainParticleLayer::initParticle leaves every new particle UNKNOWN
    // with firstVelocityQuery=true. The next moveParticles call runs the first
    // FlowTile query and only then can it enter params/residual.
    particle.flowStatus = RiverFlowStatus::Unknown;
    particle.firstVelocityQuery = true;
    particle.screenValid = true;
    // RandomParticleLayer::initParticle draws x, y and z independently from
    // their per-axis [min,max] bounds. The river shader consumes only xy.
    const Vec3 randomRange = m_settings.randomOffsetMax - m_settings.randomOffsetMin;
    particle.randomOffset =
        Vec3(m_settings.randomOffsetMin.x + randomRange.x * random01(),
             m_settings.randomOffsetMin.y + randomRange.y * random01(),
             m_settings.randomOffsetMin.z + randomRange.z * random01());
    // LifeCycleParticleLayer::initParticle sets birthDate = time; therefore
    // every new screen particle starts at age 0 and fades in.
    particle.age = 0.0f;
    particle.life = m_settings.fadeInSeconds + m_settings.activeSeconds + m_settings.fadeOutSeconds;
    m_particles.push_back(particle);
}

float RiverParticleSystem::ageFade(const RiverParticle& particle) const {
    return riverLifecycleFade(particle.age, m_settings.fadeInSeconds, m_settings.activeSeconds,
                              m_settings.fadeOutSeconds);
}

void RiverParticleSystem::beginFadeOut(RiverParticle& particle, RiverFadeReason reason) {
    const float fadeOutStart = m_settings.fadeInSeconds + m_settings.activeSeconds;
    if (!particle.fadingOut && particle.age < fadeOutStart) {
        particle.fadeOutStartFade = ageFade(particle);
        // LifeCycleParticleLayer::setFadingOut rebases birthDate to retain the
        // current intensity, so a partial particle fades over
        // intensity * fadeOutDelay, not the full delay.
        particle.fadeOutDuration = particle.fadeOutStartFade * m_settings.fadeOutSeconds;
        particle.fadeOutElapsed = 0.0f;
        particle.fadingOut = true;
    }
    particle.fadeReason = reason;
}

bool RiverParticleSystem::isFading(const RiverParticle& particle) const {
    return particle.fadingOut ||
           particle.age >= m_settings.fadeInSeconds + m_settings.activeSeconds;
}

bool RiverParticleSystem::contributesRiverParams(const RiverParticle& particle) const {
    if (!particle.worldValid) {
        return false;
    }
    // DrawRiversTask::getRiversParticlesParams keeps
    // FlowTile::UNKNOWN < status <= FlowTile::NEAR.
    return particle.flowStatus != RiverFlowStatus::Unknown &&
           particle.flowStatus <= RiverFlowStatus::Near;
}

bool RiverParticleSystem::contributesRiverResidual(const RiverParticle& particle) const {
    return particle.worldValid && (particle.flowStatus == RiverFlowStatus::Inside ||
                                   particle.flowStatus == RiverFlowStatus::Leaving);
}

float RiverParticleSystem::particleFade(const RiverParticle& particle) const {
    if (particle.fadingOut) {
        if (particle.fadeOutDuration <= 0.0f) {
            return 0.0f;
        }
        const float remaining = 1.0f - particle.fadeOutElapsed / particle.fadeOutDuration;
        return std::clamp(particle.fadeOutStartFade * remaining, 0.0f, 1.0f);
    }
    return ageFade(particle);
}

float RiverParticleSystem::particleScreenRadius(const RiverParticle&) const {
    return m_settings.particleRadius * kRiverParticleRadiusFactor;
}

void RiverParticleSystem::buildGpuParticleParams(uint16_t viewportWidth, uint16_t viewportHeight) {
    m_gpuParticleParams.assign(static_cast<std::size_t>(m_settings.maxParticles) * 8u, 0.0f);
    m_gpuParticleCount = 0;

    const float width = static_cast<float>(viewportWidth);
    const float height = static_cast<float>(viewportHeight);
    const float radius = m_settings.particleRadius * kRiverParticleRadiusFactor;
    m_gpuGridWidth = std::max(
        1u, static_cast<uint32_t>(kRiverSpriteGridFactor * width / std::max(radius, 1.0f)));
    m_gpuGridHeight = std::max(
        1u, static_cast<uint32_t>(kRiverSpriteGridFactor * height / std::max(radius, 1.0f)));
    m_gpuGridCellWidth = width / static_cast<float>(m_gpuGridWidth);
    m_gpuGridCellHeight = height / static_cast<float>(m_gpuGridHeight);
    m_gpuGridCellSize = std::min(m_gpuGridCellWidth, m_gpuGridCellHeight);
    m_gpuSpriteTable.assign(static_cast<std::size_t>(m_gpuGridWidth) * m_gpuGridHeight * kMaxParticlesPerCell, -1.0f);
    std::vector<uint32_t> rowMinX(m_gpuGridHeight, std::numeric_limits<uint32_t>::max());
    std::vector<uint32_t> rowMaxX(m_gpuGridHeight, 0u);

    for (const RiverParticle& particle : m_particles) {
        if (!contributesRiverParams(particle)) {
            continue;
        }
        const uint32_t particleIndex = m_gpuParticleCount;
        float* params = m_gpuParticleParams.data() + static_cast<std::size_t>(particleIndex) * 8u;
        params[0] = particle.screen.x;
        params[1] = particle.screen.y;
        params[2] = particle.world.x;
        params[3] = particle.world.y;
        params[4] = particle.randomOffset.x;
        params[5] = particle.randomOffset.y;
        params[6] = particleFade(particle);
        params[7] = particleScreenRadius(particle);
        ++m_gpuParticleCount;
        if (!contributesRiverResidual(particle)) {
            continue;
        }

        const float particleRadius = params[7];
        int minX = static_cast<int>(std::floor((params[0] - particleRadius) / m_gpuGridCellWidth));
        int maxX = static_cast<int>(std::floor((params[0] + particleRadius) / m_gpuGridCellWidth));
        int minY = static_cast<int>(std::floor((params[1] - particleRadius) / m_gpuGridCellHeight));
        int maxY = static_cast<int>(std::floor((params[1] + particleRadius) / m_gpuGridCellHeight));
        if (maxX < 0 || minX >= static_cast<int>(m_gpuGridWidth) || maxY < 0 ||
            minY >= static_cast<int>(m_gpuGridHeight)) {
            continue;
        }
        minX = std::clamp(minX, 0, static_cast<int>(m_gpuGridWidth) - 1);
        maxX = std::clamp(maxX, 0, static_cast<int>(m_gpuGridWidth) - 1);
        minY = std::clamp(minY, 0, static_cast<int>(m_gpuGridHeight) - 1);
        maxY = std::clamp(maxY, 0, static_cast<int>(m_gpuGridHeight) - 1);
        for (int cellY = minY; cellY <= maxY; ++cellY) {
            rowMinX[static_cast<std::size_t>(cellY)] =
                std::min(rowMinX[static_cast<std::size_t>(cellY)], static_cast<uint32_t>(minX));
            rowMaxX[static_cast<std::size_t>(cellY)] =
                std::max(rowMaxX[static_cast<std::size_t>(cellY)], static_cast<uint32_t>(maxX));
            for (int cellX = minX; cellX <= maxX; ++cellX) {
                const std::size_t cellBase =
                    (static_cast<std::size_t>(cellY) * m_gpuGridWidth + static_cast<std::size_t>(cellX)) *
                    kMaxParticlesPerCell;
                int slot = -1;
                for (uint32_t k = 0; k < kMaxParticlesPerCell; ++k) {
                    if (m_gpuSpriteTable[cellBase + k] < 0.0f) {
                        slot = static_cast<int>(k);
                        break;
                    }
                }
                if (slot < 0) {
                    float minIntensity = params[6];
                    int weakest = -1;
                    for (uint32_t k = 0; k < kMaxParticlesPerCell; ++k) {
                        const float intensity =
                            m_gpuParticleParams[static_cast<std::size_t>(m_gpuSpriteTable[cellBase + k] + 0.5f) * 8u +
                                                6u];
                        if (intensity < minIntensity) {
                            minIntensity = intensity;
                            weakest = static_cast<int>(k);
                        }
                    }
                    if (weakest < 0) {
                        continue;
                    }
                    slot = weakest;
                }
                m_gpuSpriteTable[cellBase + static_cast<std::size_t>(slot)] = static_cast<float>(particleIndex);
            }
        }
    }

    m_gpuGridSpans.clear();
    m_gpuGridSpans.reserve(m_gpuGridHeight);
    for (uint32_t row = 0; row < m_gpuGridHeight; ++row) {
        const uint32_t minX = rowMinX[static_cast<std::size_t>(row)];
        const uint32_t maxX = rowMaxX[static_cast<std::size_t>(row)];
        if (minX <= maxX) {
            m_gpuGridSpans.push_back(RiverGridSpan{row, minX, maxX});
        }
    }
}

void RiverParticleSystem::splatNormalPixels() {
    m_normalPixels = m_baseNormalPixels;
    const uint32_t size = m_settings.textureSize;
    if (m_particles.empty()) {
        buildNormalMipChain(m_normalPixels, size, m_splatNormalMips);
        return;
    }

    for (const RiverParticle& particle : m_particles) {
        if (!contributesRiverResidual(particle)) {
            continue;
        }
        Vec2 uv;
        if (!worldToRiverUv(particle.world, uv)) {
            continue;
        }
        const float textureScale = static_cast<float>(size) / 512.0f;
        const float radiusU =
            std::clamp(particleScreenRadius(particle) * textureScale, 3.0f, static_cast<float>(size) * 0.08f);
        const float radiusV = radiusU;
        const float centerX = uv.x * static_cast<float>(size);
        const float centerY = wrap01(uv.y) * static_cast<float>(size);
        const int minX = std::max(0, static_cast<int>(std::floor(centerX - radiusU)));
        const int maxX = std::min(static_cast<int>(size) - 1, static_cast<int>(std::ceil(centerX + radiusU)));
        const int minY = std::max(0, static_cast<int>(std::floor(centerY - radiusV)));
        const int maxY = std::min(static_cast<int>(size) - 1, static_cast<int>(std::ceil(centerY + radiusV)));
        if (minX > maxX || minY > maxY) {
            continue;
        }

        const float fade = particleFade(particle);
        if (fade <= 0.0f) {
            continue;
        }

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const float localX = (static_cast<float>(x) + 0.5f - centerX) / radiusU;
                const float localY = (static_cast<float>(y) + 0.5f - centerY) / radiusV;
                const float distanceSq = localX * localX + localY * localY;
                if (distanceSq >= 1.0f) {
                    continue;
                }
                const float weight = smoothFunc(1.0f - std::sqrt(distanceSq)) * fade;
                if (weight <= 0.0f) {
                    continue;
                }
                const WaveSample wave = sampleWaveTile(
                    add(Vec2(localX * 1.8f, localY * 1.8f),
                        Vec2(particle.randomOffset.x, particle.randomOffset.y)),
                    m_currentWaveFrame);
                const std::size_t index = (static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)) * 4;
                float normalX = m_normalPixels[index];
                float normalY = m_normalPixels[index + 1];
                normalX = (normalX * 0.22f + wave.slope.x * m_settings.waveSlope * weight) / (0.22f + weight);
                normalY = (normalY * 0.22f + wave.slope.y * m_settings.waveSlope * weight) / (0.22f + weight);
                m_normalPixels[index] = normalX;
                m_normalPixels[index + 1] = normalY;
            }
        }
    }
    buildNormalMipChain(m_normalPixels, size, m_splatNormalMips);
}

void RiverParticleSystem::update(float time, float deltaSeconds, const Mat4& viewProj, const Mat4& invViewProj,
                                 const Vec3& cameraPosition, uint16_t viewportWidth, uint16_t viewportHeight) {
    if (!m_valid || viewportWidth == 0 || viewportHeight == 0) {
        return;
    }
    const bool sameView =
        m_hasLastDepthView && m_lastDepthWidth == viewportWidth && m_lastDepthHeight == viewportHeight &&
        std::memcmp(m_lastDepthViewProj.values.data(), viewProj.values.data(), sizeof(float) * 16) == 0;
    m_lastDepthViewProj = viewProj;
    m_lastDepthWidth = viewportWidth;
    m_lastDepthHeight = viewportHeight;
    m_hasLastDepthView = true;
    applyPendingDepthResults();
    if (m_waveFrameCount > 1 && !m_baseNormalFrames.empty()) {
        const uint32_t frame =
            static_cast<uint32_t>(std::max(time, 0.0f) / m_settings.waveTimeStep) % m_waveFrameCount;
        if (frame != m_currentWaveFrame && frame < m_baseNormalFrames.size()) {
            m_currentWaveFrame = frame;
            m_baseNormalPixels = m_baseNormalFrames[frame];
            buildBaseNormalMips();
        }
    }
    const float dt = std::clamp(deltaSeconds, 0.0f, 0.1f);
    const float radius = m_settings.particleRadius;
    const float overlapSq = 0.96f * 4.0f * radius * radius;
    std::vector<Vec2> acceptedScreens;
    acceptedScreens.reserve(m_settings.maxParticles);
    RiverParticleGrid poissonGrid;
    poissonGrid.reset(viewportWidth, viewportHeight, radius);
    RiverParticleGrid riverStatusGrid;
    riverStatusGrid.reset(viewportWidth, viewportHeight, radius);
    std::vector<uint8_t> insideStatus(m_particles.size(), 0);

    const float width = static_cast<float>(viewportWidth);
    const float height = static_cast<float>(viewportHeight);
    const float margin = 2.0f * radius;
    for (std::size_t index = 0; index < m_particles.size(); ++index) {
        const RiverParticle& particle = m_particles[index];
        if ((!particle.worldValid && !particle.depthWorldValid) || isFading(particle) || !particle.screenValid ||
            particle.screen.x < 0.0f || particle.screen.x >= width || particle.screen.y < 0.0f ||
            particle.screen.y >= height) {
            continue;
        }
        if (particle.worldValid && particle.flowStatus == RiverFlowStatus::Inside) {
            insideStatus[index] = 1;
        }
        riverStatusGrid.add(particle.screen, 1.0f, static_cast<int>(index));
    }
    const auto hasInsideNeighbor = [&](std::size_t selfIndex, Vec2 screen) {
        for (const RiverGridEntry& entry : riverStatusGrid.cell(screen.x, screen.y)) {
            if (entry.screenIndex < 0 || static_cast<std::size_t>(entry.screenIndex) == selfIndex ||
                static_cast<std::size_t>(entry.screenIndex) >= insideStatus.size()) {
                continue;
            }
            if (insideStatus[static_cast<std::size_t>(entry.screenIndex)] != 0) {
                return true;
            }
        }
        return false;
    };
    const bool depthSnapshotUsable = sameView && depthSnapshotMatches(viewProj, viewportWidth, viewportHeight);
    uint32_t depthAttempts = 0;
    uint32_t depthSuccess = 0;

    std::size_t writeIndex = 0;
    for (std::size_t particleIndex = 0; particleIndex < m_particles.size(); ++particleIndex) {
        RiverParticle& particle = m_particles[particleIndex];
        particle.age += dt;
        if (particle.fadingOut) {
            particle.fadeOutElapsed += dt;
            if (particle.fadeOutElapsed >= particle.fadeOutDuration) {
                continue;
            }
        } else if (particle.age >= particle.life) {
            continue;
        }
        if (depthSnapshotUsable && !particle.worldValid && !particle.depthWorldValid && particle.screenValid &&
            particle.screen.x >= 0.0f && particle.screen.x < width && particle.screen.y >= 0.0f &&
            particle.screen.y < height) {
            Vec3 depthWorld;
            ++depthAttempts;
            if (screenToDepthWorld(particle.screen.x, particle.screen.y, depthWorld)) {
                particle.world = Vec2(depthWorld.x, depthWorld.z);
                particle.worldHeight = depthWorld.y;
                particle.depthWorldValid = true;
                ++depthSuccess;
            }
        }
        const bool fading = isFading(particle);
        if (particle.worldValid || particle.depthWorldValid) {
            // Port of TerrainParticleLayer::moveParticles' FlowTile state
            // machine. The original applies it to every particle with a
            // terrainPos, including positions reconstructed from the depth
            // buffer by ScreenParticleLayer. The status changes happen before
            // the position update; the neighbor test therefore uses the
            // previous frame's grid, as in ScreenParticleLayer::getNeighbors.
            const Vec2 oldVelocity = particle.leavingVelocity;
            const bool needsSample = particle.firstVelocityQuery ||
                                     particle.flowStatus == RiverFlowStatus::Inside ||
                                     particle.flowStatus == RiverFlowStatus::Leaving;
            HydroFlowSample sample;
            if (needsSample) {
                sample = m_flow.sample(particle.world);
            }
            const RiverFlowTransition transition = transitionRiverFlow(
                particle.flowStatus, particle.firstVelocityQuery, oldVelocity, sample.inside, sample.velocity,
                hasInsideNeighbor(particleIndex, particle.screen));
            if (transition.kill) {
                ++m_nearKillCount;
                continue;
            }
            particle.flowStatus = transition.status;
            particle.leavingVelocity = transition.velocity;
            particle.firstVelocityQuery = false;
            if (particle.flowStatus == RiverFlowStatus::Inside ||
                particle.flowStatus == RiverFlowStatus::Leaving) {
                if (!particle.worldValid) {
                    // A depth-derived position that samples INSIDE becomes a
                    // regular river particle, as TerrainParticleLayer does
                    // after getFlowProducer maps the world position to a tile.
                    particle.worldValid = true;
                    ++m_depthFlowPromotions;
                }
                particle.worldHeight = m_mesh.baseHeight;
                particle.world = add(particle.world, scale(particle.leavingVelocity, dt * m_settings.speedScale));
            }
        }
        if (particle.worldValid || particle.depthWorldValid) {
            const float worldY = particle.worldValid ? m_mesh.baseHeight : particle.worldHeight;
            if (!projectToScreen(Vec3(particle.world.x, worldY, particle.world.y), viewProj, viewportWidth,
                                 viewportHeight, particle.screen)) {
                continue;
            }
            if (particle.screen.x < 0.0f || particle.screen.x >= width || particle.screen.y < 0.0f ||
                particle.screen.y >= height) {
                if (particle.screen.x < -margin || particle.screen.x >= width + margin ||
                    particle.screen.y < -margin || particle.screen.y >= height + margin) {
                    continue;
                }
                beginFadeOut(particle, RiverFadeReason::OutsideViewport);
                particle.screenValid = true;
                m_particles[writeIndex++] = particle;
                continue;
            }
        }
        particle.screenValid = true;
        const bool screenInside =
            particle.screen.x >= 0.0f && particle.screen.x < width && particle.screen.y >= 0.0f &&
            particle.screen.y < height;
        if (!fading && screenInside) {
            bool overlap = false;
            for (const RiverGridEntry& entry : poissonGrid.cell(particle.screen.x, particle.screen.y)) {
                if (entry.screenIndex < 0 ||
                    static_cast<std::size_t>(entry.screenIndex) >= acceptedScreens.size()) {
                    continue;
                }
                const Vec2& screen = acceptedScreens[static_cast<std::size_t>(entry.screenIndex)];
                const float dx = particle.screen.x - screen.x;
                const float dy = particle.screen.y - screen.y;
                if (dx * dx + dy * dy < overlapSq) {
                    overlap = true;
                    break;
                }
            }
            if (overlap) {
                beginFadeOut(particle, RiverFadeReason::PoissonDisk);
            } else {
                acceptedScreens.push_back(particle.screen);
                poissonGrid.add(particle.screen, particleFade(particle),
                                static_cast<int>(acceptedScreens.size()) - 1);
            }
        }
        m_particles[writeIndex++] = particle;
    }
    m_particles.resize(writeIndex);
    if (depthAttempts > 0 && !m_depthBackfillLogged) {
        HBG_LOGI("river depth backfill: attempts={} success={} active={} river={} depth={}", depthAttempts,
                 depthSuccess, m_particles.size(), worldParticleCount(), depthWorldParticleCount());
        m_depthBackfillLogged = true;
    }
    if (m_depthFlowPromotions > 0 && !m_depthFlowPromotedLogged) {
        HBG_LOGI("river depth flow: promoted={} active={} river={} depth={}", m_depthFlowPromotions,
                 m_particles.size(), worldParticleCount(), depthWorldParticleCount());
        m_depthFlowPromotedLogged = true;
    }

    const auto spawnAtScreen = [&](float screenX, float screenY) {
        // ScreenParticleLayer::addNewParticles gets a depth-buffer worldPos for
        // every new particle, including river particles; TerrainParticleLayer
        // classifies it on the next update. Prefer the depth snapshot over the
        // screen-to-water fallback whenever the current view has one.
        if (depthSnapshotUsable) {
            Vec3 depthWorld;
            if (screenToDepthWorld(screenX, screenY, depthWorld)) {
                spawnParticle(Vec2(depthWorld.x, depthWorld.z), Vec2(screenX, screenY), false, true,
                              depthWorld.y);
            } else {
                // winz == 1 in the original leaves worldPos UNINITIALIZED.
                spawnParticle(Vec2(0.0f, 0.0f), Vec2(screenX, screenY), false);
            }
            return;
        }
        Vec2 planePosition;
        const bool river = screenToWaterPlane(screenX, screenY, invViewProj, cameraPosition, viewportWidth,
                                              viewportHeight, planePosition) &&
                           insideRiver(planePosition);
        if (river) {
            spawnParticle(planePosition, Vec2(screenX, screenY), true);
            return;
        }
        spawnParticle(Vec2(0.0f, 0.0f), Vec2(screenX, screenY), false);
    };

    AngleRangeList ranges;
    std::vector<Vec2> candidates = acceptedScreens;
    if (candidates.empty()) {
        for (const RiverParticle& particle : m_particles) {
            if (!particle.screenValid || particle.screen.x < 0.0f || particle.screen.x >= viewportWidth ||
                particle.screen.y < 0.0f || particle.screen.y >= viewportHeight) {
                continue;
            }
            if (!isFading(particle) || particle.fadeReason != RiverFadeReason::OutsideViewport) {
                candidates.push_back(particle.screen);
            }
        }
    }
    if (candidates.empty() && m_particles.size() < m_settings.maxParticles) {
        const float screenX = random01() * width;
        const float screenY = random01() * height;
        spawnAtScreen(screenX, screenY);
        acceptedScreens.push_back(Vec2(screenX, screenY));
        poissonGrid.add(acceptedScreens.back(), particleFade(m_particles.back()),
                        static_cast<int>(acceptedScreens.size()) - 1);
        candidates.push_back(acceptedScreens.back());
    }
    const float spawnDistance = 2.0f * radius;
    while (!candidates.empty() && m_particles.size() < m_settings.maxParticles) {
        const std::size_t candidateIndex =
            std::min(static_cast<std::size_t>(random01() * static_cast<float>(candidates.size())),
                     candidates.size() - 1);
        const Vec2 origin = candidates[candidateIndex];
        candidates[candidateIndex] = candidates.back();
        candidates.pop_back();

        findSpawnRanges(origin, acceptedScreens, poissonGrid.cell(origin.x, origin.y), radius, ranges);
        while (ranges.count() > 0 && m_particles.size() < m_settings.maxParticles) {
            const int rangeIndex =
                std::min(static_cast<int>(random01() * static_cast<float>(ranges.count())), ranges.count() - 1);
            const AngleRange range = ranges.at(rangeIndex);
            const float angle = range.minAngle + (range.maxAngle - range.minAngle) * random01();
            ranges.subtract(angle - kPi / 3.0f, angle + kPi / 3.0f);

            const float screenX = origin.x + std::cos(angle) * spawnDistance;
            const float screenY = origin.y + std::sin(angle) * spawnDistance;
            if (screenX < 0.0f || screenX >= width || screenY < 0.0f || screenY >= height) {
                continue;
            }
            spawnAtScreen(screenX, screenY);
            const Vec2 screen(screenX, screenY);
            acceptedScreens.push_back(screen);
            poissonGrid.add(screen, particleFade(m_particles.back()),
                            static_cast<int>(acceptedScreens.size()) - 1);
            candidates.push_back(screen);
        }
    }

    m_depthQueries.clear();
    if (!depthSnapshotUsable && !sameView) {
        m_depthQueries.reserve(m_particles.size());
        for (const RiverParticle& particle : m_particles) {
            if (particle.worldValid || particle.depthWorldValid || !particle.screenValid ||
                particle.screen.x < 0.0f || particle.screen.x >= width || particle.screen.y < 0.0f ||
                particle.screen.y >= height) {
                continue;
            }
            m_depthQueries.push_back(RiverDepthQuery{particle.id, particle.screen});
        }
    }
    if (depthSnapshotUsable) {
        m_depthRequestMode = RiverDepthRequestMode::None;
    } else if (sameView) {
        m_depthRequestMode = RiverDepthRequestMode::Full;
    } else {
        m_depthRequestMode = m_depthQueries.empty() ? RiverDepthRequestMode::None
                                                    : RiverDepthRequestMode::Particles;
    }

    buildGpuParticleParams(viewportWidth, viewportHeight);
    if (m_cpuNormalSplat) {
        splatNormalPixels();
    } else {
        m_normalPixels = m_baseNormalPixels;
    }
}

} // namespace hbg::render
