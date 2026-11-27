#include "render/river/river_mesh.h"

#include <algorithm>
#include <cmath>

namespace hbg::render {
namespace {

Vec2 subtract(Vec2 lhs, Vec2 rhs) {
    return Vec2(lhs.x - rhs.x, lhs.y - rhs.y);
}

Vec2 add(Vec2 lhs, Vec2 rhs) {
    return Vec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

Vec2 scale(Vec2 value, float factor) {
    return Vec2(value.x * factor, value.y * factor);
}

float lengthSq(Vec2 value) {
    return value.x * value.x + value.y * value.y;
}

Vec2 normalize(Vec2 value) {
    const float length = std::sqrt(lengthSq(value));
    return length > 1.0e-8f ? scale(value, 1.0f / length) : Vec2(0.0f, 1.0f);
}

struct AxisSample {
    Vec2 position;
    Vec2 tangent;
};

AxisSample sampleAxis(const std::vector<Vec2>& axis, float t) {
    if (axis.size() < 2) {
        return {axis.empty() ? Vec2(0.0f, 0.0f) : axis.front(), Vec2(0.0f, 1.0f)};
    }

    float totalLength = 0.0f;
    for (std::size_t index = 1; index < axis.size(); ++index) {
        totalLength += std::sqrt(lengthSq(subtract(axis[index], axis[index - 1])));
    }
    if (totalLength <= 1.0e-6f) {
        return {axis.front(), Vec2(0.0f, 1.0f)};
    }

    const float target = std::clamp(t, 0.0f, 1.0f) * totalLength;
    float accumulated = 0.0f;
    for (std::size_t index = 1; index < axis.size(); ++index) {
        const Vec2 delta = subtract(axis[index], axis[index - 1]);
        const float segmentLength = std::sqrt(lengthSq(delta));
        if (accumulated + segmentLength >= target || index + 1 == axis.size()) {
            const float local = segmentLength > 1.0e-6f
                                    ? (target - accumulated) / segmentLength
                                    : 0.0f;
            return {add(axis[index - 1], scale(delta, std::clamp(local, 0.0f, 1.0f))),
                    normalize(delta)};
        }
        accumulated += segmentLength;
    }
    return {axis.back(), normalize(subtract(axis.back(), axis[axis.size() - 2]))};
}

} // namespace

asset::Mesh buildRiverMesh(const RiverGraph& graph, const RiverMeshSettings& settings, uint32_t materialIndex,
                           const std::string& name) {
    asset::Mesh mesh;
    mesh.name = name;
    if (graph.axis.size() < 2 || graph.width <= 0.0f) {
        return mesh;
    }

    const int segments = std::max(settings.segments, 1);
    const int crossSegments = std::max(settings.crossSegments, 1);
    const int stride = crossSegments + 1;
    const float halfWidth = graph.width * 0.5f;
    const AxisSample first = sampleAxis(graph.axis, 0.0f);

    mesh.vertices.reserve(static_cast<std::size_t>(segments + 1) * stride);
    for (int segment = 0; segment <= segments; ++segment) {
        const float t = static_cast<float>(segment) / static_cast<float>(segments);
        const AxisSample sample = sampleAxis(graph.axis, t);
        const Vec2 normal(-sample.tangent.y, sample.tangent.x);

        for (int cross = 0; cross <= crossSegments; ++cross) {
            const float u = static_cast<float>(cross) / static_cast<float>(crossSegments);
            const float offset = (u * 2.0f - 1.0f) * halfWidth;
            const Vec2 point = add(sample.position, scale(normal, offset));

            asset::MeshVertex vertex;
            vertex.position = Vec3(point.x, settings.baseHeight, point.y);
            vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
            vertex.tangent = Vec4(sample.tangent.x, 0.0f, sample.tangent.y, 1.0f);
            vertex.texcoord0 = Vec2(u, (point.y - first.position.y) / std::max(settings.uvLength, 1.0e-3f));
            mesh.vertices.push_back(vertex);
        }
    }

    mesh.indices.reserve(static_cast<std::size_t>(segments) * crossSegments * 6);
    for (int segment = 0; segment < segments; ++segment) {
        for (int cross = 0; cross < crossSegments; ++cross) {
            const uint32_t a = static_cast<uint32_t>(segment * stride + cross);
            const uint32_t b = a + 1;
            const uint32_t c = a + static_cast<uint32_t>(stride);
            const uint32_t d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
    }

    mesh.hasNormals = true;
    mesh.hasTangents = true;
    mesh.hasUv0 = true;
    mesh.recomputeBounds();

    asset::SubMesh subMesh;
    subMesh.indexOffset = 0;
    subMesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    subMesh.materialIndex = materialIndex;
    subMesh.bounds = mesh.bounds;
    mesh.submeshes.push_back(subMesh);
    return mesh;
}

} // namespace hbg::render
