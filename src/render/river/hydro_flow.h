#pragma once

#include "core/math.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace hbg::render {

// CPU equivalent of the river hydro graph used by Proland's HydroCurve.
// Coordinates are graph coordinates: x is east and y is north (mapped to
// world x/z when the graph is authored).
struct RiverCurve {
    std::vector<Vec2> points;
    float potential = 0.0f;
    float width = 0.0f;
};

struct RiverGraph {
    std::vector<Vec2> axis;
    float width = 0.0f;
    RiverCurve leftBank;
    RiverCurve rightBank;

    static RiverGraph straight(float length, float width, int segments = 64);
    static RiverGraph meander(float length, float width, float amplitude, float waves, int segments = 128);
    static RiverGraph fromControlPoints(const std::vector<Vec2>& controlPoints, float width, int segments = 320);
};

struct HydroFlowSample {
    Vec2 velocity;
    float potential = 0.0f;
    bool inside = false;
};

// Port of the potential interpolation and velocity finite differences from
// Proland river/sources/proland/rivers/HydroFlowTile.cpp. The original code
// caches one potential per cache-cell corner and evaluates the four corners
// of the cell containing the query; this graph keeps the same bank-distance
// weighting and cell-corner cache for a single river with two banks.
class HydroFlow {
  public:
    HydroFlow() = default;
    explicit HydroFlow(RiverGraph graph, float slip = 1.3f, float searchRadiusFactor = 1.0f);

    HydroFlowSample sample(Vec2 position) const;
    bool inside(Vec2 position) const;
    const RiverGraph& graph() const { return m_graph; }
    float potentialStep() const { return m_step; }
    std::size_t potentialCacheSize() const { return m_potentialCache.size(); }

  private:
    float distanceToCurve(const RiverCurve& curve, Vec2 position) const;
    bool samplePotential(Vec2 position, float& potential) const;
    bool cachedPotential(long long cellX, long long cellY, float& potential) const;

    RiverGraph m_graph;
    float m_slip = 1.3f;
    float m_searchRadiusFactor = 1.0f;
    float m_step = 1.0f;
    mutable std::unordered_map<std::uint64_t, float> m_potentialCache;
};

} // namespace hbg::render
