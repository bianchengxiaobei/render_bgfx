#include "asset/mesh.h"

#include "mikktspace.h"

#include <algorithm>
#include <cmath>

namespace hbg::asset {
namespace {

Vec3 subtract(const Vec3& lhs, const Vec3& rhs) {
    return Vec3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
}

Vec3 crossProduct(const Vec3& lhs, const Vec3& rhs) {
    return Vec3(lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x);
}

void normalize(Vec3& value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length > 1.0e-8f) {
        value.x /= length;
        value.y /= length;
        value.z /= length;
    }
}

struct MikkUserData {
    Mesh* mesh = nullptr;
};

int mikkGetNumFaces(const SMikkTSpaceContext* context) {
    const auto* userData = static_cast<const MikkUserData*>(context->m_pUserData);
    return static_cast<int>(userData->mesh->indices.size() / 3);
}

int mikkGetNumVerticesOfFace(const SMikkTSpaceContext*, int) { return 3; }

void mikkGetPosition(const SMikkTSpaceContext* context, float out[3], int face, int vertex) {
    const auto* userData = static_cast<const MikkUserData*>(context->m_pUserData);
    const MeshVertex& meshVertex =
        userData->mesh->vertices[userData->mesh->indices[static_cast<std::size_t>(face * 3 + vertex)]];
    out[0] = meshVertex.position.x;
    out[1] = meshVertex.position.y;
    out[2] = meshVertex.position.z;
}

void mikkGetNormal(const SMikkTSpaceContext* context, float out[3], int face, int vertex) {
    const auto* userData = static_cast<const MikkUserData*>(context->m_pUserData);
    const MeshVertex& meshVertex =
        userData->mesh->vertices[userData->mesh->indices[static_cast<std::size_t>(face * 3 + vertex)]];
    out[0] = meshVertex.normal.x;
    out[1] = meshVertex.normal.y;
    out[2] = meshVertex.normal.z;
}

void mikkGetTexCoord(const SMikkTSpaceContext* context, float out[2], int face, int vertex) {
    const auto* userData = static_cast<const MikkUserData*>(context->m_pUserData);
    const MeshVertex& meshVertex =
        userData->mesh->vertices[userData->mesh->indices[static_cast<std::size_t>(face * 3 + vertex)]];
    out[0] = meshVertex.texcoord0.x;
    out[1] = meshVertex.texcoord0.y;
}

void mikkSetTSpaceBasic(const SMikkTSpaceContext* context, const float tangent[3], float sign, int face, int vertex) {
    auto* userData = static_cast<MikkUserData*>(context->m_pUserData);
    MeshVertex& meshVertex =
        userData->mesh->vertices[userData->mesh->indices[static_cast<std::size_t>(face * 3 + vertex)]];
    meshVertex.tangent = Vec4(tangent[0], tangent[1], tangent[2], sign);
}

} // namespace

Bounds Mesh::computeBounds(const std::vector<MeshVertex>& vertices) {
    Bounds result;
    for (const MeshVertex& vertex : vertices) {
        result.expand(vertex.position);
    }
    return result;
}

void Mesh::recomputeBounds() {
    bounds = computeBounds(vertices);
}

void Mesh::generateNormals() {
    if (hasNormals || vertices.empty() || indices.size() < 3) {
        return;
    }

    for (std::size_t index = 0; index + 2 < indices.size(); index += 3) {
        MeshVertex& a = vertices[indices[index + 0]];
        MeshVertex& b = vertices[indices[index + 1]];
        MeshVertex& c = vertices[indices[index + 2]];

        const Vec3 normal =
            crossProduct(subtract(b.position, a.position), subtract(c.position, a.position));
        const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length <= 1.0e-8f) {
            continue;
        }

        const Vec3 weighted(normal.x / length, normal.y / length, normal.z / length);
        for (MeshVertex* vertex : {&a, &b, &c}) {
            vertex->normal.x += weighted.x;
            vertex->normal.y += weighted.y;
            vertex->normal.z += weighted.z;
        }
    }

    for (MeshVertex& vertex : vertices) {
        normalize(vertex.normal);
        if (vertex.normal.x == 0.0f && vertex.normal.y == 0.0f && vertex.normal.z == 0.0f) {
            vertex.normal = Vec3(0.0f, 0.0f, 1.0f);
        }
    }
    hasNormals = true;
}

void Mesh::generateTangents() {
    if (hasTangents || !hasUv0 || indices.size() < 3) {
        return;
    }
    if (!hasNormals) {
        generateNormals();
    }

    MikkUserData userData{this};
    SMikkTSpaceInterface interface{};
    interface.m_getNumFaces = mikkGetNumFaces;
    interface.m_getNumVerticesOfFace = mikkGetNumVerticesOfFace;
    interface.m_getPosition = mikkGetPosition;
    interface.m_getNormal = mikkGetNormal;
    interface.m_getTexCoord = mikkGetTexCoord;
    interface.m_setTSpaceBasic = mikkSetTSpaceBasic;

    SMikkTSpaceContext context{};
    context.m_pInterface = &interface;
    context.m_pUserData = &userData;

    hasTangents = genTangSpaceDefault(&context) != 0;
}

void Mesh::applyMorphWeights(const std::vector<float>& weights, std::vector<MeshVertex>& output) const {
    output = vertices;
    const std::size_t targetCount = std::min(morphTargets.size(), weights.size());
    if (targetCount == 0) {
        return;
    }

    bool hasMorphedNormals = false;
    bool hasMorphedTangents = false;
    for (std::size_t targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
        const float weight = weights[targetIndex];
        if (std::abs(weight) <= 1.0e-8f) {
            continue;
        }
        const MorphTarget& target = morphTargets[targetIndex];
        if (target.positionDeltas.size() < output.size()) {
            continue;
        }
        hasMorphedNormals = hasMorphedNormals || target.hasNormals;
        hasMorphedTangents = hasMorphedTangents || target.hasTangents;
        for (std::size_t vertex = 0; vertex < output.size(); ++vertex) {
            MeshVertex& result = output[vertex];
            const Vec3 positionDelta = target.positionDeltas[vertex] * weight;
            result.position = result.position + positionDelta;
            if (target.hasNormals && vertex < target.normalDeltas.size()) {
                result.normal = result.normal + target.normalDeltas[vertex] * weight;
            }
            if (target.hasTangents && vertex < target.tangentDeltas.size()) {
                const Vec4 delta = target.tangentDeltas[vertex];
                result.tangent.x += delta.x * weight;
                result.tangent.y += delta.y * weight;
                result.tangent.z += delta.z * weight;
            }
        }
    }

    if (hasMorphedNormals) {
        for (MeshVertex& vertex : output) {
            const Vec3 baseNormal = vertex.normal;
            normalize(vertex.normal);
            if (length(baseNormal) <= 1.0e-8f) {
                vertex.normal = Vec3(0.0f, 0.0f, 1.0f);
            }
        }
    }
    if (hasMorphedTangents) {
        for (MeshVertex& vertex : output) {
            Vec3 tangent(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z);
            normalize(tangent);
            vertex.tangent = Vec4(tangent.x, tangent.y, tangent.z, vertex.tangent.w);
        }
    }
}

void Mesh::applySkinning(const std::vector<Mat4>& jointMatrices, std::vector<MeshVertex>& output) const {
    if (skinVertices.size() != vertices.size()) {
        return;
    }
    if (output.size() != vertices.size()) {
        output = vertices;
    }

    for (std::size_t vertexIndex = 0; vertexIndex < output.size(); ++vertexIndex) {
        const MeshVertex& source = output[vertexIndex];
        const SkinVertex& skin = skinVertices[vertexIndex];
        Vec3 position(0.0f, 0.0f, 0.0f);
        Vec3 normal(0.0f, 0.0f, 0.0f);
        Vec3 tangent(0.0f, 0.0f, 0.0f);
        float totalWeight = 0.0f;
        const std::size_t influenceBegin = skin.influenceOffset;
        const std::size_t influenceEnd =
            std::min(skinInfluences.size(), influenceBegin + static_cast<std::size_t>(skin.influenceCount));
        for (std::size_t influenceIndex = influenceBegin; influenceIndex < influenceEnd; ++influenceIndex) {
            const SkinInfluence& influence = skinInfluences[influenceIndex];
            const float weight = influence.weight;
            if (std::abs(weight) <= 1.0e-8f) {
                continue;
            }
            const uint32_t joint = influence.joint;
            if (joint >= jointMatrices.size()) {
                continue;
            }
            const Mat4& matrix = jointMatrices[joint];
            const Vec3 sourceTangent(source.tangent.x, source.tangent.y, source.tangent.z);
            position = position + transformPoint(matrix, source.position) * weight;
            normal = normal + transformDirection(matrix, source.normal) * weight;
            tangent = tangent + transformDirection(matrix, sourceTangent) * weight;
            totalWeight += weight;
        }
        if (totalWeight <= 1.0e-8f) {
            continue;
        }
        if (std::abs(totalWeight - 1.0f) > 1.0e-5f) {
            position = position * (1.0f / totalWeight);
        }
        output[vertexIndex].position = position;
        if (length(normal) > 1.0e-8f) {
            output[vertexIndex].normal = hbg::normalize(normal);
        }
        if (length(tangent) > 1.0e-8f) {
            const Vec3 normalizedTangent = hbg::normalize(tangent);
            output[vertexIndex].tangent =
                Vec4(normalizedTangent.x, normalizedTangent.y, normalizedTangent.z, source.tangent.w);
        }
    }
}

} // namespace hbg::asset
