#pragma once

#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/texture_loader.h"
#include "core/bounds.h"
#include "core/math.h"
#include "render/atmosphere/local_fog_volume.h"
#include "render/scene/decal.h"
#include "render/scene/light.h"
#include "render/scene/reflection_probe.h"
#include "render/scene/render_object.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hbg::render {

struct Camera {
    enum class Projection : uint8_t { Perspective, Orthographic };

    std::string name;
    Projection projection = Projection::Perspective;
    Mat4 world = Mat4::identity();
    float yfov = 1.0471975512f;
    float aspect = 1.7777778f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    float xmag = 1.0f;
    float ymag = 1.0f;
};

struct RenderScene {
    std::string sourcePath;
    std::vector<asset::Mesh> meshes;
    std::vector<asset::Material> materials;
    std::vector<asset::ImageAsset> images;
    std::vector<RenderObject> objects;
    std::vector<Light> lights;
    std::vector<LocalFogVolume> localFogVolumes;
    std::vector<Decal> decals;
    std::vector<ReflectionProbe> probes;
    std::vector<Camera> cameras;
    Bounds bounds;

    void recomputeBounds();
};

} // namespace hbg::render
