#include "asset/mesh.h"

#include "mikktspace.h"

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

void Mesh::recomputeBounds() {
    bounds = {};
    for (const MeshVertex& vertex : vertices) {
        bounds.expand(vertex.position);
    }
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

} // namespace hbg::asset
