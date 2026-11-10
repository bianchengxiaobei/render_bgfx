#pragma once

#include "core/bounds.h"
#include "core/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hbg::asset {

struct MeshVertex {
    Vec3 position;
    Vec3 normal{0.0f, 0.0f, 1.0f};
    Vec4 tangent{0.0f, 0.0f, 0.0f, 1.0f};
    Vec2 texcoord0;
    Vec2 texcoord1;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct SubMesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
    Bounds bounds;
};

struct Mesh {
    std::string name;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> submeshes;
    Bounds bounds;
    bool hasNormals = false;
    bool hasTangents = false;
    bool hasUv0 = false;
    bool hasUv1 = false;
    bool hasColors = false;

    void recomputeBounds();
    void generateNormals();
    void generateTangents();
};

} // namespace hbg::asset
