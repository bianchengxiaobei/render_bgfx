#pragma once

#include "render/river/hydro_flow.h"
#include "render/river/river_mesh.h"
#include "render/river/river_terrain.h"

#include <cstdint>

namespace hbg::render {

// Screen-space sprite parameters for the river particle producer. The
// original Proland river plugin keeps a Poisson-disk distribution whose
// density is constant in screen space; this prototype follows
// ScreenParticleLayer's full-viewport cloud. Particles whose screen ray hits
// the river get a water-plane world position and feed the residual; the rest
// borrow a world position from the scene depth snapshot, or remain screen-only
// when no depth is available.
struct RiverParticleSettings {
    float speedScale = 800.0f;
    // Proland's ScreenParticleLayer radius is a screen-space radius; the
    // demo uses 15 px and the sprite quad expands it by 3x.
    float particleRadius = 15.0f;
    // Proland river1/helloworld.xml: particleStorage capacity=5000 and
    // lifeCycleParticleLayer fadeInDelay=2, activeDelay=30, fadeOutDelay=2.
    uint32_t maxParticles = 5000;
    float fadeInSeconds = 2.0f;
    float activeSeconds = 30.0f;
    float fadeOutSeconds = 2.0f;
    // Proland river1/helloworld.xml randomParticleLayer sets
    // xmin/ymin/zmin=-1 and leaves the max bounds at their +1 defaults.
    // RandomParticleLayer::initParticle samples each axis independently.
    Vec3 randomOffsetMin = Vec3(-1.0f, -1.0f, -1.0f);
    Vec3 randomOffsetMax = Vec3(1.0f, 1.0f, 1.0f);
    uint32_t textureSize = 256;
    uint32_t waveTileSize = 256;
    uint32_t waveTimeLoop = 8;
    float waveTimeStep = 5.0f;
    float waveSlope = 0.18f;
    // Proland demo uses drawRivers waveSlopeFactor=10; the wave tile stores
    // raw signed slopes and the shader builds normalize(vec3(slopes * factor, 1)).
    float waveSlopeFactor = 10.0f;
    float waveLength = 40.0f;
    // Retained for config compatibility; new particles now use Proland's
    // single random candidate and 2 * radius angular growth.
    float spawnCellFactor = 2.0f;
};

struct RiverSpec {
    bool enabled = false;
    bool editable = false;
    float refractionStrength = 0.0f;
    float absorptionStrength = 0.0f;
    float reflectionStrength = 0.0f;
    RiverGraph graph;
    RiverMeshSettings mesh;
    RiverTerrainSettings terrain;
    uint32_t materialIndex = UINT32_MAX;
    uint32_t meshIndex = UINT32_MAX;
    uint32_t objectIndex = UINT32_MAX;
    RiverParticleSettings particles;
};

} // namespace hbg::render
