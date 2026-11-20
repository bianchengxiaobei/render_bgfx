#pragma once

#include "asset/mesh.h"
#include "render/scene/render_scene.h"

#include "hbg/dawn/dawn_protocol.h"

#include <cstdint>

namespace hbg::dawn {

struct MeshExportOptions {
    bool useObjectMeshForMetaPass = false;
    float albedoBoost = 1.0f;
    float rayOffset = 0.0f;
    bool castShadows = true;
    UInt2 uv2MapSize{0, 0};
    UInt2 uv2Offset{0, 0};
    bool normalizeUv2 = true;
    uint32_t meshInstanceVisibilityMask = 0x7Fu;
};

struct MaterialExportOptions {
    bool srgbToLinear = true;
    bool flipNormalY = false;
    float specular = 0.5f;
    float ior = 1.0f;
};

struct LightExportOptions {
    bool srgbToLinear = false;
    bool directLighting = true;
    float indirectIntensity = 1.0f;
    float indirectMultiplier = 1.0f;
    float directionalSourceAngleDegrees = 0.0f;
    float sourceRadius = 0.0f;
    float sourceLength = 0.0f;
    float areaBarnDoorAngleDegrees = 88.0f;
    float areaBarnDoorLength = 0.2f;
};

struct SkyLightExportOptions {
    uint32_t cubeSize = 512;
    Vec3 rotationEulerDegrees{0.0f, 0.0f, 0.0f};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float indirectMultiplier = 1.0f;
    float indirectIntensity = 1.0f;
    bool srgbToLinear = true;
    float shadowTranslucency = 0.0f;
    Vec3 envColor{0.0f, 0.0f, 0.0f};
};

Guid makeMeshGuid(uint32_t meshIndex);
Guid makeInstanceGuid(uint32_t instanceIndex);

MeshInfo exportMesh(const asset::Mesh& mesh, uint32_t lodIndex, const Guid& guid,
                    const MeshExportOptions& options);

MeshInstanceInfo exportMeshInstance(const asset::Mesh& mesh, const render::RenderObject& object,
                                    uint32_t meshIndex, uint32_t instanceIndex,
                                    const MeshExportOptions& options);

MaterialInfo exportMaterial(const asset::Material& material, const render::RenderScene& scene,
                            const MaterialExportOptions& options);

LightFullInfo exportLight(const render::Light& light, const LightExportOptions& options,
                          uint32_t lightIndex = 0);

SkyLightInfo exportSkyLight(const asset::ImageAsset* source, const SkyLightExportOptions& options);

}  // namespace hbg::dawn
