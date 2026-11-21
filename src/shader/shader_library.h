#pragma once

#include "gfx/resource_manager.h"
#include "shader/shader_key.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace hbg::shader {

class ShaderLibrary {
  public:
    ShaderLibrary() = default;
    ~ShaderLibrary() = default;

    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;

    bool init(gfx::ResourceManager& resources, const std::filesystem::path& shaderRoot);
    void shutdown(gfx::ResourceManager& resources);
    bool valid() const { return m_valid; }

    gfx::ProgramHandle program(PassId pass, uint64_t materialKey, uint64_t pipelineKey = 0);
    gfx::ProgramHandle program(const ProgramKey& key) {
        return program(key.pass, key.materialKey, key.pipelineKey);
    }
    std::string_view backendName() const { return m_backend; }

  private:
    struct PassFiles {
        std::string vertex;
        std::vector<std::string> fragments;
        std::vector<std::string> bufferFragments;
        std::string compute;
        std::string bufferCompute;
        std::vector<std::string> computes;
        std::vector<std::string> bufferComputes;
    };

    struct BackendFiles {
        PassFiles forward;
        PassFiles forwardAdvanced;
        PassFiles forwardInstanced;
        PassFiles bakedGI;
        PassFiles bakedGICompensate;
        PassFiles bakedGIForwardComposite;
        PassFiles tonemap;
        PassFiles postProcessExposure;
        PassFiles postProcessExposurePrepass;
        PassFiles postProcessExposureReduce;
        PassFiles postProcessExposureReduceFinal;
        PassFiles postProcessHistogramClear;
        PassFiles postProcessHistogramGen;
        PassFiles postProcessHistogramReduce;
        PassFiles applyExposure;
        PassFiles debugExposure;
        PassFiles final;
        PassFiles uberPost;
        PassFiles debugColorGrading;
        PassFiles bloomPrefilter;
        PassFiles bloomBlur;
        PassFiles bloomDownsample;
        PassFiles bloomUpsample;
        PassFiles debugBloom;
        PassFiles taa;
        PassFiles taaSharpen;
        PassFiles debugTaaHistory;
        PassFiles dofCoc;
        PassFiles dofGather;
        PassFiles dofCombine;
        PassFiles debugDofCoc;
        PassFiles debugFxaa;
        PassFiles motionBlur;
        PassFiles shadow;
        PassFiles shadowInstanced;
        PassFiles shadowCull;
        PassFiles shadowMoments;
        PassFiles shadowSatHorizontal;
        PassFiles shadowSatVertical;
        PassFiles shadowEvsmBlur;
        PassFiles shadowEvsmBlurMoments;
        PassFiles prepass;
        PassFiles motionVectors;
        PassFiles cameraMotionVectors;
        PassFiles depthPyramidCopy;
        PassFiles depthPyramidDownsample;
        PassFiles sky;
        PassFiles volumetricFog;
        PassFiles volumetricFogComposite;
        PassFiles debugLightData;
        PassFiles debugNormal;
        PassFiles debugMotionVectors;
        PassFiles debugDepth;
        PassFiles debugDepthPyramid;
        PassFiles debugLightCluster;
        PassFiles debugShadowAtlas;
        PassFiles debugColor;
        PassFiles debugLightmap;
        PassFiles debugAlbedo;
        PassFiles debugRoughness;
        PassFiles debugGbuffer;
        PassFiles wireframe;
        PassFiles gbuffer;
        PassFiles gbufferClear;
        PassFiles gbufferDecals;
        PassFiles decal;
        PassFiles decalClear;
        PassFiles deferredLighting;
        PassFiles deferredLightingAdvanced;
        PassFiles deferredLightingVariant;
        PassFiles deferredLightingVariantCompute;
        PassFiles ssao;
        PassFiles ssaoBlur;
        PassFiles ssaoTemporal;
        PassFiles ssaoUpsample;
        PassFiles debugSsao;
        PassFiles ssrTrace;
        PassFiles ssrTemporal;
        PassFiles ssrComposite;
        PassFiles debugSsr;
        PassFiles debugDecals;
        PassFiles ssgiTrace;
        PassFiles ssgiDenoise;
        PassFiles ssgiTemporal;
        PassFiles ssgiComposite;
        PassFiles debugSsgi;
        PassFiles debugSsgiRaw;
        PassFiles contactShadows;
        PassFiles litMaterialFlags;
        PassFiles litVariantIndirectClear;
        PassFiles litVariantIndirect;
        PassFiles lightScreenAabb;
        PassFiles lightBigTile;
        PassFiles lightFptl;
        PassFiles lightCluster;
        PassFiles lightScreenAabbBuffer;
        PassFiles lightBigTileBuffer;
        PassFiles lightFptlBuffer;
        PassFiles lightClusterBuffer;
        PassFiles lightIndirectArgs;
        PassFiles debugContactShadows;
        PassFiles debugShadowCascade;
        PassFiles debugShadowFilter;
        PassFiles ui;
    };

    bool loadManifest(const std::filesystem::path& manifestPath);
    gfx::ProgramHandle createProgram(const PassFiles& files, uint32_t variant, bool bufferPath, bool lightLayers);
    gfx::ProgramHandle createComputeProgram(const PassFiles& files, bool bufferPath, uint32_t variant);

    gfx::ResourceManager* m_resources = nullptr;
    std::filesystem::path m_root;
    std::string m_backend;
    BackendFiles m_files;
    bool m_valid = false;
    std::unordered_map<uint64_t, gfx::ProgramHandle> m_programs;
};

} // namespace hbg::shader
