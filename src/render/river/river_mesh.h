#pragma once

#include "asset/mesh.h"
#include "render/river/hydro_flow.h"

#include <cstdint>
#include <string>

namespace hbg::render {

struct RiverMeshSettings {
    int segments = 320;
    int crossSegments = 12;
    float baseHeight = 0.05f;
    float uvLength = 180.0f;
};

// Builds the flat water ribbon used by the river demo. The centerline and
// width come from the same RiverGraph used by HydroFlow, so particle
// advection and the rendered surface share one river description.
asset::Mesh buildRiverMesh(const RiverGraph& graph, const RiverMeshSettings& settings, uint32_t materialIndex,
                           const std::string& name = "RiverSurface");

} // namespace hbg::render
