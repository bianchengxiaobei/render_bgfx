#pragma once

#include "core/bounds.h"
#include "core/math.h"

#include <cstddef>
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
    Vec3 previousPosition{};
};

struct SubMesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
    Bounds bounds;
};

struct MorphTarget {
    std::string name;
    std::vector<Vec3> positionDeltas;
    std::vector<Vec3> normalDeltas;
    std::vector<Vec4> tangentDeltas;
    bool hasNormals = false;
    bool hasTangents = false;
};

inline constexpr std::size_t kSkinInfluencesPerSet = 4;

struct SkinInfluence {
    uint16_t joint = 0;
    float weight = 0.0f;
};

struct SkinVertex {
    uint32_t influenceOffset = 0;
    uint32_t influenceCount = 0;
};

struct Skin {
    std::string name;
    std::vector<uint32_t> joints;
    std::vector<Mat4> inverseBindMatrices;
};

struct Mesh {
    std::string name;
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> submeshes;
    std::vector<MorphTarget> morphTargets;
    std::vector<float> morphWeights;
    std::vector<SkinVertex> skinVertices;
    std::vector<SkinInfluence> skinInfluences;
    Bounds bounds;
    bool hasNormals = false;
    bool hasTangents = false;
    bool hasUv0 = false;
    bool hasUv1 = false;
    bool hasColors = false;

    static Bounds computeBounds(const std::vector<MeshVertex>& vertices);
    void recomputeBounds();
    void generateNormals();
    void generateTangents();
    void applyMorphWeights(const std::vector<float>& weights, std::vector<MeshVertex>& output) const;
    void applySkinning(const std::vector<Mat4>& jointMatrices, std::vector<MeshVertex>& output) const;
};

} // namespace hbg::asset
