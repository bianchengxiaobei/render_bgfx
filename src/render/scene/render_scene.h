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

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hbg::render {

struct LightmapAtlasSource {
    std::string file;
    uint32_t size = 0;
    uint32_t layers = 3;
    std::string format = "rgba16f";
};

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

enum class VfxPropertyKind : uint8_t {
    Float,
    Vector2,
    Vector3,
    Vector4,
    Color,
};

struct VfxPropertyValue {
    std::string name;
    VfxPropertyKind kind = VfxPropertyKind::Float;
    std::array<float, 4> values{};
};

struct VfxInstance {
    std::string name;
    std::filesystem::path asset;
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    bool enabled = true;
    bool playOnAwake = true;
    float playRate = 1.0f;
    uint32_t randomSeed = 0;
    std::vector<VfxPropertyValue> properties;
};

Mat4 vfxInstanceWorld(const VfxInstance& instance);

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
    std::vector<VfxInstance> vfxInstances;
    std::vector<Camera> cameras;
    std::vector<LightmapAtlasSource> lightmapAtlases;
    std::filesystem::path lightmapBaseDirectory;
    Bounds bounds;

    bool hasLightmap() const { return !lightmapAtlases.empty(); }
    void recomputeBounds();
};

} // namespace hbg::render
