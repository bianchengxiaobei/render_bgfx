#pragma once

#include "core/math.h"
#include "render/river/river_spec.h"

#include <cstdint>
#include <vector>

namespace hbg::render {

constexpr float kRiverParticleRadiusFactor = 3.0f;
constexpr float kRiverSpriteGridFactor = 6.0f;

enum class RiverFadeReason : uint8_t {
    Age,
    OutsideViewport,
    PoissonDisk,
};

// Port of Proland's FlowTile status values used by TerrainParticleLayer.
// UNKNOWN/INSIDE/LEAVING/NEAR/OUTSIDE follow the original numeric order.
enum class RiverFlowStatus : uint8_t {
    Unknown = 0,
    Inside = 1,
    Leaving = 2,
    Near = 3,
    Outside = 4,
};

struct RiverFlowTransition {
    RiverFlowStatus status = RiverFlowStatus::Unknown;
    Vec2 velocity;
    // TerrainParticleLayer calls lifeCycleLayer->killParticle() for NEAR, so
    // the particle is removed by removeOldParticles in the same update.
    bool kill = false;
};

// Pure port of the status/velocity conditions in
// TerrainParticleLayer::moveParticles. Kept separate from the particle
// storage so every FlowTile transition can be tested in isolation.
RiverFlowTransition transitionRiverFlow(RiverFlowStatus status, bool firstVelocityQuery, Vec2 oldVelocity,
                                        bool sampleInside, Vec2 sampleVelocity, bool insideNeighbor);

// Port of LifeCycleParticleLayer::getIntensity: fade in, hold, fade out.
float riverLifecycleFade(float age, float fadeIn, float active, float fadeOut);

enum class RiverDepthRequestMode : uint8_t {
    None,
    Full,
    Particles,
};

struct RiverDepthQuery {
    uint32_t particleId = 0;
    Vec2 screen;
};

struct RiverGridSpan {
    uint32_t row = 0;
    uint32_t minX = 0;
    uint32_t maxX = 0;
};

struct RiverParticle {
    uint32_t id = 0;
    Vec2 world;
    Vec2 screen;
    Vec3 randomOffset;
    Vec2 leavingVelocity;
    float worldHeight = 0.0f;
    float age = 0.0f;
    float life = 0.0f; // fadeIn + active + fadeOut
    float fadeOutElapsed = 0.0f;
    float fadeOutDuration = 0.0f;
    float fadeOutStartFade = 1.0f;
    RiverFadeReason fadeReason = RiverFadeReason::Age;
    RiverFlowStatus flowStatus = RiverFlowStatus::Unknown;
    bool worldValid = false;
    bool depthWorldValid = false;
    bool screenValid = false;
    bool fadingOut = false;
    bool firstVelocityQuery = true;
};

// CPU prototype of Proland's screen-space river particle system. As in
// ScreenParticleLayer, the Poisson-disk cloud covers the whole viewport. New
// particles borrow a world position from the scene depth snapshot (the
// original depth-buffer worldPos path) whenever the current view has one;
// only when no depth is available do river rays fall back to the flat water
// surface. Particles whose depth sample lies inside the river join the
// FlowTile state machine and residual, as TerrainParticleLayer does.
class RiverParticleSystem {
  public:
    static constexpr uint32_t kMaxParticlesPerCell = 16;
    static constexpr uint32_t kSpriteLayers = 4;

    RiverParticleSystem() = default;

    bool init(const RiverGraph& graph, const RiverMeshSettings& mesh, const RiverParticleSettings& settings);
    void update(float time, float deltaSeconds, const Mat4& viewProj, const Mat4& invViewProj,
                const Vec3& cameraPosition, uint16_t viewportWidth, uint16_t viewportHeight);

    bool valid() const { return m_valid; }
    void setCpuNormalSplatEnabled(bool enabled) { m_cpuNormalSplat = enabled; }
    bool cpuNormalSplatEnabled() const { return m_cpuNormalSplat; }
    uint32_t textureSize() const { return m_settings.textureSize; }
    uint32_t particleCount() const { return static_cast<uint32_t>(m_particles.size()); }
    uint32_t worldParticleCount() const;
    uint32_t depthWorldParticleCount() const;
    uint32_t leavingParticleCount() const;
    uint32_t nearParticleCount() const;
    uint32_t nearKillCount() const { return m_nearKillCount; }
    uint32_t depthFlowPromotionCount() const { return m_depthFlowPromotions; }
    uint32_t outsideParticleCount() const;
    uint32_t fadingParticleCount() const;
    uint32_t particleCapacity() const { return m_settings.maxParticles; }
    void setDepthSnapshot(std::vector<float> depths, uint16_t width, uint16_t height, const Mat4& viewProj);
    void clearDepthSnapshot();
    bool hasDepthSnapshot() const { return !m_depthDepths.empty(); }
    bool depthSnapshotMatches(const Mat4& viewProj) const;
    bool depthSnapshotMatches(const Mat4& viewProj, uint16_t width, uint16_t height) const;
    const Mat4& depthSnapshotViewProj() const { return m_depthViewProj; }
    RiverDepthRequestMode depthRequestMode() const { return m_depthRequestMode; }
    std::vector<RiverDepthQuery> takeDepthQueries();
    void submitParticleDepths(std::vector<float> depths, std::vector<RiverDepthQuery> queries, const Mat4& viewProj,
                              uint16_t width, uint16_t height);
    bool hasPendingDepthResults() const { return m_hasPendingDepthResults; }
    const std::vector<RiverParticle>& particles() const { return m_particles; }
    const std::vector<uint8_t>& baseColorPixels() const { return m_baseColorPixels; }
    const std::vector<float>& baseNormalPixels() const { return m_baseNormalPixels; }
    const std::vector<std::vector<float>>& baseNormalMips() const { return m_baseNormalMips; }
    const std::vector<std::vector<float>>& normalMips() const {
        return m_cpuNormalSplat ? m_splatNormalMips : m_baseNormalMips;
    }
    const std::vector<float>& normalPixels() const { return m_normalPixels; }
    const std::vector<float>& gpuParticleParams() const { return m_gpuParticleParams; }
    const std::vector<float>& gpuSpriteTable() const { return m_gpuSpriteTable; }
    const std::vector<RiverGridSpan>& gpuGridSpans() const { return m_gpuGridSpans; }
    uint32_t gpuParticleCount() const { return m_gpuParticleCount; }
    uint32_t gpuParticleCapacity() const { return m_settings.maxParticles; }
    uint32_t gpuGridWidth() const { return m_gpuGridWidth; }
    uint32_t gpuGridHeight() const { return m_gpuGridHeight; }
    float gpuGridCellWidth() const { return m_gpuGridCellWidth; }
    float gpuGridCellHeight() const { return m_gpuGridCellHeight; }
    float gpuGridCellSize() const { return m_gpuGridCellSize; }
    uint32_t waveFrame() const { return m_currentWaveFrame; }
    uint32_t waveFrameCount() const { return m_waveFrameCount; }
    float baseHeight() const { return m_mesh.baseHeight; }
    float waveLength() const { return m_settings.waveLength; }

    bool projectToScreen(const Vec3& world, const Mat4& viewProj, uint16_t viewportWidth, uint16_t viewportHeight,
                         Vec2& screen) const;
    bool screenToRiver(float screenX, float screenY, const Mat4& invViewProj, const Vec3& cameraPosition,
                       uint16_t viewportWidth, uint16_t viewportHeight, Vec2& riverPosition) const;
    bool screenToWaterPlane(float screenX, float screenY, const Mat4& invViewProj, const Vec3& cameraPosition,
                            uint16_t viewportWidth, uint16_t viewportHeight, Vec2& waterPosition) const;
    bool screenToDepthWorld(float screenX, float screenY, Vec3& worldPosition) const;
    bool worldToRiverUv(Vec2 worldPosition, Vec2& uv) const;

  private:
    struct WaveSample {
        Vec2 slope;
    };

    bool insideRiver(Vec2 position) const;
    void buildTextures();
    void buildWaveFrames();
    void buildBaseColorPixels();
    void buildBaseHeightField(uint32_t size, uint32_t frame, std::vector<float>& heights) const;
    void encodeBaseNormalLevel(const std::vector<float>& heights, uint32_t size, std::vector<float>& pixels) const;
    void buildBaseNormalFrame(uint32_t frame);
    void buildBaseNormalMips();
    void buildNormalMipChain(const std::vector<float>& source, uint32_t size,
                             std::vector<std::vector<float>>& mips) const;
    void splatNormalPixels();
    void buildGpuParticleParams(uint16_t viewportWidth, uint16_t viewportHeight);
    void applyPendingDepthResults();
    bool depthToWorld(float screenX, float screenY, float depth, const Mat4& invViewProj, uint16_t width,
                      uint16_t height, Vec3& worldPosition) const;
    float ageFade(const RiverParticle& particle) const;
    bool isFading(const RiverParticle& particle) const;
    bool contributesRiverParams(const RiverParticle& particle) const;
    bool contributesRiverResidual(const RiverParticle& particle) const;
    float particleFade(const RiverParticle& particle) const;
    void beginFadeOut(RiverParticle& particle, RiverFadeReason reason);
    float particleScreenRadius(const RiverParticle& particle) const;
    WaveSample sampleWaveTile(Vec2 uv, uint32_t frame) const;
    float sampleWaveHeight(Vec2 uv, uint32_t frame) const;
    void spawnParticle(Vec2 worldPosition, Vec2 screenPosition, bool riverValid, bool depthWorldValid = false,
                       float worldHeight = 0.0f);
    float random01() const;

    RiverGraph m_graph;
    RiverMeshSettings m_mesh;
    RiverParticleSettings m_settings;
    HydroFlow m_flow;
    std::vector<RiverParticle> m_particles;
    std::vector<std::vector<float>> m_waveSlopeFrames;
    std::vector<std::vector<float>> m_waveHeightFrames;
    std::vector<std::vector<float>> m_baseNormalFrames;
    std::vector<uint8_t> m_baseColorPixels;
    std::vector<float> m_baseNormalPixels;
    std::vector<std::vector<float>> m_baseNormalMips;
    std::vector<std::vector<float>> m_splatNormalMips;
    std::vector<float> m_normalPixels;
    uint32_t m_waveFrameCount = 1;
    uint32_t m_currentWaveFrame = 0;
    struct PendingDepthResult {
        uint32_t particleId = 0;
        Vec2 screen;
        float depth = 1.0f;
    };
    std::vector<float> m_depthDepths;
    Mat4 m_depthViewProj;
    Mat4 m_depthInvViewProj;
    uint16_t m_depthWidth = 0;
    uint16_t m_depthHeight = 0;
    std::vector<RiverDepthQuery> m_depthQueries;
    std::vector<PendingDepthResult> m_pendingDepthResults;
    Mat4 m_lastDepthViewProj;
    Mat4 m_pendingDepthViewProj;
    uint16_t m_lastDepthWidth = 0;
    uint16_t m_lastDepthHeight = 0;
    uint16_t m_pendingDepthWidth = 0;
    uint16_t m_pendingDepthHeight = 0;
    uint32_t m_nextParticleId = 1;
    RiverDepthRequestMode m_depthRequestMode = RiverDepthRequestMode::None;
    std::vector<float> m_gpuParticleParams;
    std::vector<float> m_gpuSpriteTable;
    std::vector<RiverGridSpan> m_gpuGridSpans;
    uint32_t m_gpuParticleCount = 0;
    uint32_t m_gpuGridWidth = 0;
    uint32_t m_gpuGridHeight = 0;
    float m_gpuGridCellWidth = 0.0f;
    float m_gpuGridCellHeight = 0.0f;
    float m_gpuGridCellSize = 0.0f;
    uint32_t m_nearKillCount = 0;
    uint32_t m_depthFlowPromotions = 0;
    bool m_cpuNormalSplat = true;
    bool m_valid = false;
    bool m_depthBackfillLogged = false;
    bool m_depthPackAppliedLogged = false;
    bool m_depthFlowPromotedLogged = false;
    bool m_hasLastDepthView = false;
    bool m_hasPendingDepthResults = false;
    mutable uint32_t m_randomState = 0x9e3779b9u;
};

} // namespace hbg::render
