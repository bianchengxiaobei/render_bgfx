#pragma once

#include "asset/mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hbg::dawn {

struct LightmapUvOptions {
    float texelsPerUnit = 6.0f;
    uint32_t resolution = 0;
    uint32_t padding = 2;
    bool bilinear = true;
    bool bruteForce = false;
    uint32_t maxChartSize = 0;

    float normalDeviationWeight = 2.0f;
    float normalSeamWeight = 4.0f;
    float maxCost = 2.0f;
    uint32_t maxIterations = 1;
    bool useInputMeshUvs = false;
    bool fixWinding = false;
};

struct LightmapUvLayout {
    asset::Mesh mesh;
    std::vector<uint32_t> sourceVertex;
    std::vector<Vec2> uvTexels;
    Vec2 uvMinTexels;
    Vec2 uvMaxTexels;
    uint32_t atlasWidth = 0;
    uint32_t atlasHeight = 0;
    uint32_t atlasCount = 0;
    uint32_t chartCount = 0;
    float texelsPerUnit = 0.0f;
    float naturalSize = 0.0f;
    float surfaceArea = 0.0f;
};

LightmapUvLayout unwrapLightmapUv(const asset::Mesh& mesh, const LightmapUvOptions& options = {});

bool tryUnwrapLightmapUv(const asset::Mesh& mesh, const LightmapUvOptions& options, LightmapUvLayout& outLayout,
                         std::string* warning = nullptr);

} // namespace hbg::dawn
