#include "hbg/dawn/dawn_unwrap.h"

#include "core/log.h"
#include "core/math.h"

#include "xatlas.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hbg::dawn {
namespace {

struct AtlasDeleter {
    void operator()(xatlas::Atlas* atlas) const {
        if (atlas != nullptr) {
            xatlas::Destroy(atlas);
        }
    }
};

using AtlasPtr = std::unique_ptr<xatlas::Atlas, AtlasDeleter>;

float triangleArea(const Vec3& a, const Vec3& b, const Vec3& c) {
    return 0.5f * length(cross(b - a, c - a));
}

float meshSurfaceArea(const asset::Mesh& mesh) {
    float surfaceArea = 0.0f;
    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3u) {
        const Vec3& a = mesh.vertices[mesh.indices[triangle + 0u]].position;
        const Vec3& b = mesh.vertices[mesh.indices[triangle + 1u]].position;
        const Vec3& c = mesh.vertices[mesh.indices[triangle + 2u]].position;
        surfaceArea += triangleArea(a, b, c);
    }
    return surfaceArea;
}

} // namespace

LightmapUvLayout unwrapLightmapUv(const asset::Mesh& mesh, const LightmapUvOptions& options) {
    if (mesh.vertices.empty()) {
        throw std::runtime_error("cannot unwrap UV2 for an empty mesh");
    }
    if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u) {
        throw std::runtime_error("cannot unwrap UV2 for a mesh without triangle indices");
    }

    std::vector<float> positions;
    std::vector<float> normals;
    positions.reserve(mesh.vertices.size() * 3u);
    if (mesh.hasNormals) {
        normals.reserve(mesh.vertices.size() * 3u);
    }
    for (const asset::MeshVertex& vertex : mesh.vertices) {
        positions.push_back(vertex.position.x);
        positions.push_back(vertex.position.y);
        positions.push_back(vertex.position.z);
        if (mesh.hasNormals) {
            normals.push_back(vertex.normal.x);
            normals.push_back(vertex.normal.y);
            normals.push_back(vertex.normal.z);
        }
    }

    xatlas::MeshDecl meshDecl;
    meshDecl.vertexPositionData = positions.data();
    meshDecl.vertexPositionStride = sizeof(float) * 3u;
    if (mesh.hasNormals) {
        meshDecl.vertexNormalData = normals.data();
        meshDecl.vertexNormalStride = sizeof(float) * 3u;
    }
    meshDecl.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    meshDecl.indexData = mesh.indices.data();
    meshDecl.indexCount = static_cast<uint32_t>(mesh.indices.size());
    meshDecl.indexFormat = xatlas::IndexFormat::UInt32;

    AtlasPtr atlas(xatlas::Create());
    const xatlas::AddMeshError addError = xatlas::AddMesh(atlas.get(), meshDecl, 1u);
    if (addError != xatlas::AddMeshError::Success) {
        throw std::runtime_error(std::string("xatlas AddMesh failed: ") + xatlas::StringForEnum(addError));
    }

    xatlas::ChartOptions chartOptions;
    chartOptions.normalDeviationWeight = options.normalDeviationWeight;
    chartOptions.normalSeamWeight = options.normalSeamWeight;
    chartOptions.maxCost = options.maxCost;
    chartOptions.maxIterations = options.maxIterations;
    chartOptions.useInputMeshUvs = options.useInputMeshUvs;
    chartOptions.fixWinding = options.fixWinding;

    xatlas::PackOptions packOptions;
    packOptions.maxChartSize = options.maxChartSize;
    packOptions.padding = options.padding;
    packOptions.texelsPerUnit = options.texelsPerUnit;
    packOptions.resolution = options.resolution;
    packOptions.bilinear = options.bilinear;
    packOptions.bruteForce = options.bruteForce;

    xatlas::Generate(atlas.get(), chartOptions, packOptions);
    if (atlas->meshCount != 1u || atlas->meshes == nullptr) {
        throw std::runtime_error("xatlas did not produce one output mesh");
    }
    if (atlas->width == 0u || atlas->height == 0u) {
        throw std::runtime_error("xatlas produced an empty atlas");
    }

    const xatlas::Mesh& output = atlas->meshes[0];
    if (output.vertexCount == 0u || output.vertexArray == nullptr) {
        throw std::runtime_error("xatlas produced no vertices");
    }
    if (output.indexCount != mesh.indices.size() || output.indexArray == nullptr) {
        throw std::runtime_error("xatlas changed the triangle index count");
    }

    LightmapUvLayout result;
    result.mesh = mesh;
    result.mesh.vertices.clear();
    result.mesh.indices.clear();
    result.mesh.vertices.reserve(output.vertexCount);
    result.mesh.indices.reserve(output.indexCount);
    result.sourceVertex.reserve(output.vertexCount);
    result.uvTexels.reserve(output.vertexCount);
    result.atlasWidth = atlas->width;
    result.atlasHeight = atlas->height;
    result.atlasCount = atlas->atlasCount;
    result.chartCount = atlas->chartCount;
    result.texelsPerUnit = atlas->texelsPerUnit;

    const float atlasWidth = static_cast<float>(atlas->width);
    const float atlasHeight = static_cast<float>(atlas->height);
    result.uvMinTexels = Vec2(std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    result.uvMaxTexels = Vec2(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
    for (uint32_t index = 0; index < output.vertexCount; ++index) {
        const xatlas::Vertex& source = output.vertexArray[index];
        if (source.xref < 0 || static_cast<uint32_t>(source.xref) >= mesh.vertices.size()) {
            throw std::runtime_error("xatlas output vertex references an invalid source vertex");
        }
        if (source.uv[0] < 0.0f || source.uv[1] < 0.0f || source.uv[0] > atlasWidth || source.uv[1] > atlasHeight) {
            throw std::runtime_error("xatlas output UV is outside the atlas");
        }

        asset::MeshVertex vertex = mesh.vertices[static_cast<std::size_t>(source.xref)];
        result.mesh.vertices.push_back(vertex);
        result.sourceVertex.push_back(static_cast<uint32_t>(source.xref));
        result.uvTexels.push_back(Vec2(source.uv[0], source.uv[1]));
        result.uvMinTexels.x = std::min(result.uvMinTexels.x, source.uv[0]);
        result.uvMinTexels.y = std::min(result.uvMinTexels.y, source.uv[1]);
        result.uvMaxTexels.x = std::max(result.uvMaxTexels.x, source.uv[0]);
        result.uvMaxTexels.y = std::max(result.uvMaxTexels.y, source.uv[1]);
    }

    const float uvSpan =
        std::max(result.uvMaxTexels.x - result.uvMinTexels.x, result.uvMaxTexels.y - result.uvMinTexels.y);
    if (uvSpan <= 0.0f) {
        throw std::runtime_error("xatlas produced a degenerate UV layout");
    }
    // Normalize uniformly to the largest chart extent. This preserves chart aspect
    // and xatlas's texel density when the chart is moved into a square lightmap chunk.
    for (std::size_t index = 0; index < result.mesh.vertices.size(); ++index) {
        result.mesh.vertices[index].texcoord1 = Vec2((result.uvTexels[index].x - result.uvMinTexels.x) / uvSpan,
                                                     (result.uvTexels[index].y - result.uvMinTexels.y) / uvSpan);
    }

    result.mesh.indices.assign(output.indexArray, output.indexArray + output.indexCount);
    result.mesh.hasUv1 = true;
    result.mesh.recomputeBounds();

    float surfaceArea = 0.0f;
    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3u) {
        const Vec3& a = mesh.vertices[mesh.indices[triangle + 0u]].position;
        const Vec3& b = mesh.vertices[mesh.indices[triangle + 1u]].position;
        const Vec3& c = mesh.vertices[mesh.indices[triangle + 2u]].position;
        surfaceArea += triangleArea(a, b, c);
    }

    result.naturalSize =
        std::ceil(std::max(result.uvMaxTexels.x - result.uvMinTexels.x, result.uvMaxTexels.y - result.uvMinTexels.y));
    result.surfaceArea = surfaceArea;
    return result;
}

bool tryUnwrapLightmapUv(const asset::Mesh& mesh, const LightmapUvOptions& options, LightmapUvLayout& outLayout,
                         std::string* warning) {
    const auto skip = [&mesh, warning](std::string reason) {
        if (warning != nullptr) {
            *warning = reason;
        }
        HBG_LOGW("lightmap UV2 skipped for '{}': {}", mesh.name, reason);
        return false;
    };

    if (mesh.vertices.empty()) {
        return skip("empty mesh");
    }
    if (mesh.indices.empty()) {
        return skip("mesh has no triangle indices");
    }
    if (mesh.indices.size() % 3u != 0u) {
        return skip("triangle index count is not divisible by 3");
    }
    for (std::size_t index = 0; index < mesh.indices.size(); ++index) {
        if (mesh.indices[index] >= mesh.vertices.size()) {
            return skip("triangle index " + std::to_string(index) + " is out of range");
        }
    }
    if (meshSurfaceArea(mesh) <= 1.0e-8f) {
        return skip("zero surface area");
    }

    try {
        LightmapUvLayout layout = unwrapLightmapUv(mesh, options);
        if (layout.surfaceArea <= 1.0e-8f) {
            return skip("zero surface area");
        }
        outLayout = std::move(layout);
        return true;
    } catch (const std::exception& error) {
        return skip(std::string("xatlas failed: ") + error.what());
    }
}

} // namespace hbg::dawn
