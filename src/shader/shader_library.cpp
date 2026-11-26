#include "shader/shader_library.h"

#include "core/log.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string_view>
#include <utility>

namespace hbg::shader {
namespace {

std::string backendKey(bgfx::RendererType::Enum type) {
    switch (type) {
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return "dxbc";
    case bgfx::RendererType::Vulkan:
        return "spirv";
    default:
        return {};
    }
}

uint64_t cacheKey(PassId pass, uint32_t variant, uint64_t pipelineKey) {
    return (pipelineKey & (kLightListBufferProgramKey | kLightLayersProgramKey)) |
           (static_cast<uint64_t>(pass) << 32) | variant;
}

} // namespace

bool ShaderLibrary::init(gfx::ResourceManager& resources, const std::filesystem::path& shaderRoot) {
    m_resources = &resources;
    m_root = shaderRoot;
    m_backend = backendKey(bgfx::getRendererType());
    if (m_backend.empty()) {
        HBG_LOGE("shader library: unsupported renderer type {}", static_cast<int>(bgfx::getRendererType()));
        return false;
    }

    const std::filesystem::path manifestPath = m_root / "manifest.json";
    if (!loadManifest(manifestPath) || !m_valid) {
        HBG_LOGE("shader library: failed to load manifest '{}'", manifestPath.string());
        return false;
    }

    HBG_LOGI("shader library: backend={} root={}", m_backend, m_root.string());
    return true;
}

bool ShaderLibrary::loadManifest(const std::filesystem::path& manifestPath) {
    std::ifstream file(manifestPath);
    if (!file.good()) {
        HBG_LOGE("shader manifest not found: {}", manifestPath.string());
        return false;
    }

    nlohmann::json root;
    try {
        file >> root;
    } catch (const std::exception& exception) {
        HBG_LOGE("shader manifest parse failed: {}", exception.what());
        return false;
    }

    const auto passes = root.find("passes");
    if (passes == root.end() || !passes->is_object()) {
        HBG_LOGE("shader manifest has no 'passes' object");
        return false;
    }
    const auto backend = passes->find(m_backend);
    if (backend == passes->end() || !backend->is_object()) {
        HBG_LOGE("shader manifest has no '{}' backend", m_backend);
        return false;
    }

    const auto readCompute = [&](const nlohmann::json& object) {
        PassFiles files;
        const auto compute = object.find("compute");
        if (compute != object.end() && compute->is_string()) {
            files.compute = compute->get<std::string>();
        }
        const auto bufferCompute = object.find("bufferCompute");
        if (bufferCompute != object.end() && bufferCompute->is_string()) {
            files.bufferCompute = bufferCompute->get<std::string>();
        }
        const auto computes = object.find("computes");
        if (computes != object.end() && computes->is_array()) {
            for (const auto& compute : *computes) {
                if (compute.is_string()) {
                    files.computes.push_back(compute.get<std::string>());
                }
            }
        }
        const auto bufferComputes = object.find("bufferComputes");
        if (bufferComputes != object.end() && bufferComputes->is_array()) {
            for (const auto& compute : *bufferComputes) {
                if (compute.is_string()) {
                    files.bufferComputes.push_back(compute.get<std::string>());
                }
            }
        }
        return files;
    };

    const auto readFiles = [&](const nlohmann::json& object, bool hasFragments) {
        PassFiles files;
        const auto vertex = object.find("vertex");
        if (vertex == object.end() || !vertex->is_string()) {
            return files;
        }
        files.vertex = vertex->get<std::string>();

        if (hasFragments) {
            const auto fragments = object.find("fragments");
            if (fragments == object.end() || !fragments->is_array()) {
                return files;
            }
            for (const auto& fragment : *fragments) {
                if (fragment.is_string()) {
                    files.fragments.push_back(fragment.get<std::string>());
                }
            }
            const auto bufferFragments = object.find("bufferFragments");
            if (bufferFragments != object.end() && bufferFragments->is_array()) {
                for (const auto& fragment : *bufferFragments) {
                    if (fragment.is_string()) {
                        files.bufferFragments.push_back(fragment.get<std::string>());
                    }
                }
            }
        } else {
            const auto fragment = object.find("fragment");
            if (fragment == object.end() || !fragment->is_string()) {
                return files;
            }
            files.fragments.push_back(fragment->get<std::string>());
            const auto bufferFragment = object.find("bufferFragment");
            if (bufferFragment != object.end() && bufferFragment->is_string()) {
                files.bufferFragments.push_back(bufferFragment->get<std::string>());
            }
        }
        return files;
    };

    const auto forward = backend->find("forward");
    const auto forwardAdvanced = backend->find("forwardAdvanced");
    const auto forwardInstanced = backend->find("forwardInstanced");
    const auto bindlessForward = backend->find("bindlessForward");
    const auto bakedGI = backend->find("bakedGI");
    const auto bakedGICompensate = backend->find("bakedGICompensate");
    const auto bakedGIForwardComposite = backend->find("bakedGIForwardComposite");
    const auto tonemap = backend->find("tonemap");
    const auto postProcessExposure = backend->find("postProcessExposure");
    const auto exposurePrepass = backend->find("postProcessExposurePrepass");
    const auto exposureReduce = backend->find("postProcessExposureReduce");
    const auto exposureReduceFinal = backend->find("postProcessExposureReduceFinal");
    const auto histogramClear = backend->find("postProcessHistogramClear");
    const auto histogramGen = backend->find("postProcessHistogramGen");
    const auto histogramReduce = backend->find("postProcessHistogramReduce");
    const auto applyExposurePass = backend->find("applyExposure");
    const auto debugExposurePass = backend->find("debugExposure");
    const auto finalPass = backend->find("final");
    const auto uberPostPass = backend->find("uberPost");
    const auto debugColorGradingPass = backend->find("debugColorGrading");
    const auto bloomPrefilterPass = backend->find("bloomPrefilter");
    const auto bloomBlurPass = backend->find("bloomBlur");
    const auto bloomDownsamplePass = backend->find("bloomDownsample");
    const auto bloomUpsamplePass = backend->find("bloomUpsample");
    const auto debugBloomPass = backend->find("debugBloom");
    const auto taaPass = backend->find("taa");
    const auto taaSharpenPass = backend->find("taaSharpen");
    const auto debugTaaHistoryPass = backend->find("debugTaaHistory");
    const auto dofCocPass = backend->find("dofCoc");
    const auto dofGatherPass = backend->find("dofGather");
    const auto dofCombinePass = backend->find("dofCombine");
    const auto debugDofCocPass = backend->find("debugDofCoc");
    const auto debugFxaaPass = backend->find("debugFxaa");
    const auto motionBlurPass = backend->find("motionBlur");
    const auto shadow = backend->find("shadow");
    const auto shadowInstanced = backend->find("shadowInstanced");
    const auto shadowCull = backend->find("shadowCull");
    const auto gpuDrivenCull = backend->find("gpuDrivenCull");
    const auto gpuDrivenTransparentSort = backend->find("gpuDrivenTransparentSort");
    const auto skinning = backend->find("skinning");
    const auto shadowMoments = backend->find("shadowMoments");
    const auto shadowSatHorizontal = backend->find("shadowSatHorizontal");
    const auto shadowSatVertical = backend->find("shadowSatVertical");
    const auto shadowEvsmBlur = backend->find("shadowEvsmBlur");
    const auto shadowEvsmBlurMoments = backend->find("shadowEvsmBlurMoments");
    const auto shadowCopyDepth = backend->find("shadowCopyDepth");
    const auto prepass = backend->find("prepass");
    const auto motionVectors = backend->find("motionVectors");
    const auto transparentMotionVectors = backend->find("transparentMotionVectors");
    const auto cameraMotionVectors = backend->find("cameraMotionVectors");
    const auto depthPyramidCopy = backend->find("depthPyramidCopy");
    const auto depthPyramidDownsample = backend->find("depthPyramidDownsample");
    const auto depthPyramidCompute = backend->find("depthPyramidCompute");
    const auto depthPyramidMaxCompute = backend->find("depthPyramidMaxCompute");
    const auto sky = backend->find("sky");
    const auto volumetricFog = backend->find("volumetricFog");
    const auto volumetricFogComposite = backend->find("volumetricFogComposite");
    const auto debugLightData = backend->find("debugLightData");
    const auto debugNormal = backend->find("debugNormal");
    const auto debugMotionVectors = backend->find("debugMotionVectors");
    const auto debugDepth = backend->find("debugDepth");
    const auto debugDepthPyramid = backend->find("debugDepthPyramid");
    const auto debugLightCluster = backend->find("debugLightCluster");
    const auto debugShadowAtlas = backend->find("debugShadowAtlas");
    const auto debugColor = backend->find("debugColor");
    const auto debugLightmap = backend->find("debugLightmap");
    const auto debugAlbedo = backend->find("debugAlbedo");
    const auto debugRoughness = backend->find("debugRoughness");
    const auto debugGbuffer = backend->find("debugGbuffer");
    const auto wireframe = backend->find("wireframe");
    const auto gbuffer = backend->find("gbuffer");
    const auto gbufferInstanced = backend->find("gbufferInstanced");
    const auto gbufferClear = backend->find("gbufferClear");
    const auto gbufferDecals = backend->find("gbufferDecals");
    const auto decal = backend->find("decal");
    const auto decalClear = backend->find("decalClear");
    const auto deferredLighting = backend->find("deferredLighting");
    const auto deferredLightingAdvanced = backend->find("deferredLightingAdvanced");
    const auto deferredLightingAdvancedIms = backend->find("deferredLightingAdvancedIms");
    const auto deferredLightingVariant = backend->find("deferredLightingVariant");
    const auto deferredLightingVariantIms = backend->find("deferredLightingVariantIms");
    const auto deferredLightingVariantCompute = backend->find("deferredLightingVariantCompute");
    const auto deferredLightingVariantComputeIms = backend->find("deferredLightingVariantComputeIms");
    const auto litMaterialFlags = backend->find("litMaterialFlags");
    const auto litVariantIndirectClear = backend->find("litVariantIndirectClear");
    const auto litVariantIndirect = backend->find("litVariantIndirect");
    const auto ssao = backend->find("ssao");
    const auto ssaoBlur = backend->find("ssaoBlur");
    const auto ssaoTemporal = backend->find("ssaoTemporal");
    const auto ssaoUpsample = backend->find("ssaoUpsample");
    const auto debugSsao = backend->find("debugSsao");
    const auto ssrTrace = backend->find("ssrTrace");
    const auto ssrTemporal = backend->find("ssrTemporal");
    const auto ssrComposite = backend->find("ssrComposite");
    const auto debugSsr = backend->find("debugSsr");
    const auto debugDecals = backend->find("debugDecals");
    const auto ssgiTrace = backend->find("ssgiTrace");
    const auto ssgiDenoise = backend->find("ssgiDenoise");
    const auto ssgiTemporal = backend->find("ssgiTemporal");
    const auto ssgiComposite = backend->find("ssgiComposite");
    const auto debugSsgi = backend->find("debugSsgi");
    const auto debugSsgiRaw = backend->find("debugSsgiRaw");
    const auto contactShadows = backend->find("contactShadows");
    const auto lightScreenAabb = backend->find("lightScreenAabb");
    const auto lightBigTile = backend->find("lightBigTile");
    const auto lightFptl = backend->find("lightFptl");
    const auto lightCluster = backend->find("lightCluster");
    const auto lightScreenAabbBuffer = backend->find("lightScreenAabbBuffer");
    const auto lightBigTileBuffer = backend->find("lightBigTileBuffer");
    const auto lightFptlBuffer = backend->find("lightFptlBuffer");
    const auto lightClusterBuffer = backend->find("lightClusterBuffer");
    const auto lightIndirectArgs = backend->find("lightIndirectArgs");
    const auto debugContactShadows = backend->find("debugContactShadows");
    const auto debugShadowCascade = backend->find("debugShadowCascade");
    const auto debugShadowFilter = backend->find("debugShadowFilter");
    const auto ui = backend->find("ui");
    if (forward == backend->end() || forwardAdvanced == backend->end() ||
        forwardInstanced == backend->end() || bakedGI == backend->end() ||
        bakedGICompensate == backend->end() || bakedGIForwardComposite == backend->end() ||
        tonemap == backend->end() ||
        shadow == backend->end() || shadowInstanced == backend->end() || shadowCull == backend->end() ||
        skinning == backend->end() ||
        shadowMoments == backend->end() || shadowSatHorizontal == backend->end() ||
        shadowSatVertical == backend->end() || shadowEvsmBlur == backend->end() ||
        shadowEvsmBlurMoments == backend->end() || shadowCopyDepth == backend->end() ||
        prepass == backend->end() ||
        motionVectors == backend->end() || transparentMotionVectors == backend->end() ||
        cameraMotionVectors == backend->end() ||
        depthPyramidCopy == backend->end() || depthPyramidDownsample == backend->end() ||
        sky == backend->end() ||
        volumetricFog == backend->end() || volumetricFogComposite == backend->end() ||
        debugLightData == backend->end() ||
        debugNormal == backend->end() || debugMotionVectors == backend->end() ||
        debugDepth == backend->end() || debugDepthPyramid == backend->end() || debugLightCluster == backend->end() ||
        debugShadowAtlas == backend->end() || debugColor == backend->end() ||
        debugLightmap == backend->end() || debugAlbedo == backend->end() ||
        debugRoughness == backend->end() || debugGbuffer == backend->end() || wireframe == backend->end() ||
        gbufferDecals == backend->end() ||
        decal == backend->end() || decalClear == backend->end() || deferredLighting == backend->end() ||
        deferredLightingAdvanced == backend->end() || deferredLightingAdvancedIms == backend->end() ||
        deferredLightingVariant == backend->end() ||
        deferredLightingVariantIms == backend->end() ||
        deferredLightingVariantCompute == backend->end() ||
        deferredLightingVariantComputeIms == backend->end() || litMaterialFlags == backend->end() ||
        litVariantIndirectClear == backend->end() || litVariantIndirect == backend->end() ||
        ssao == backend->end() || ssaoBlur == backend->end() || ssaoTemporal == backend->end() ||
        ssaoUpsample == backend->end() || debugSsao == backend->end() || ssrTrace == backend->end() ||
        ssrTemporal == backend->end() || ssrComposite == backend->end() ||
        debugSsr == backend->end() || debugDecals == backend->end() || ssgiTrace == backend->end() ||
        ssgiDenoise == backend->end() || ssgiTemporal == backend->end() ||
        ssgiComposite == backend->end() ||
        debugSsgi == backend->end() || debugSsgiRaw == backend->end() || contactShadows == backend->end() ||
        lightScreenAabbBuffer == backend->end() || lightBigTileBuffer == backend->end() ||
        lightFptlBuffer == backend->end() || lightClusterBuffer == backend->end() ||
        lightIndirectArgs == backend->end() ||
        debugContactShadows == backend->end() || debugShadowCascade == backend->end() ||
        debugShadowFilter == backend->end() || taaPass == backend->end() || taaSharpenPass == backend->end() ||
        debugTaaHistoryPass == backend->end() || dofCocPass == backend->end() ||
        dofGatherPass == backend->end() || dofCombinePass == backend->end() ||
        debugDofCocPass == backend->end() || debugFxaaPass == backend->end() ||
        motionBlurPass == backend->end() || ui == backend->end()) {
        HBG_LOGE("shader manifest backend '{}' is incomplete", m_backend);
        return false;
    }

    m_files.forward = readFiles(*forward, true);
    m_files.forwardAdvanced = readFiles(*forwardAdvanced, true);
    m_files.forwardInstanced = readFiles(*forwardInstanced, true);
    if (bindlessForward != backend->end()) {
        m_files.bindlessForward = readFiles(*bindlessForward, true);
    }
    m_files.bakedGI = readFiles(*bakedGI, true);
    m_files.bakedGICompensate = readFiles(*bakedGICompensate, false);
    m_files.bakedGIForwardComposite = readFiles(*bakedGIForwardComposite, false);
    m_files.tonemap = readFiles(*tonemap, false);
    if (postProcessExposure != backend->end()) {
        m_files.postProcessExposure = readCompute(*postProcessExposure);
    }
    if (exposurePrepass != backend->end()) {
        m_files.postProcessExposurePrepass = readCompute(*exposurePrepass);
    }
    if (exposureReduce != backend->end()) {
        m_files.postProcessExposureReduce = readCompute(*exposureReduce);
    }
    if (exposureReduceFinal != backend->end()) {
        m_files.postProcessExposureReduceFinal = readCompute(*exposureReduceFinal);
    }
    if (histogramClear != backend->end()) {
        m_files.postProcessHistogramClear = readCompute(*histogramClear);
    }
    if (histogramGen != backend->end()) {
        m_files.postProcessHistogramGen = readCompute(*histogramGen);
    }
    if (histogramReduce != backend->end()) {
        m_files.postProcessHistogramReduce = readCompute(*histogramReduce);
    }
    if (applyExposurePass != backend->end()) {
        m_files.applyExposure = readFiles(*applyExposurePass, false);
    }
    if (debugExposurePass != backend->end()) {
        m_files.debugExposure = readFiles(*debugExposurePass, false);
    }
    if (finalPass != backend->end()) {
        m_files.final = readFiles(*finalPass, true);
    }
    if (uberPostPass != backend->end()) {
        m_files.uberPost = readFiles(*uberPostPass, false);
    }
    if (debugColorGradingPass != backend->end()) {
        m_files.debugColorGrading = readFiles(*debugColorGradingPass, false);
    }
    if (bloomPrefilterPass != backend->end()) {
        m_files.bloomPrefilter = readFiles(*bloomPrefilterPass, true);
    }
    if (bloomBlurPass != backend->end()) {
        m_files.bloomBlur = readFiles(*bloomBlurPass, true);
    }
    if (bloomDownsamplePass != backend->end()) {
        m_files.bloomDownsample = readFiles(*bloomDownsamplePass, false);
    }
    if (bloomUpsamplePass != backend->end()) {
        m_files.bloomUpsample = readFiles(*bloomUpsamplePass, true);
    }
    if (debugBloomPass != backend->end()) {
        m_files.debugBloom = readFiles(*debugBloomPass, false);
    }
    if (taaPass != backend->end()) {
        m_files.taa = readFiles(*taaPass, false);
    }
    if (taaSharpenPass != backend->end()) {
        m_files.taaSharpen = readFiles(*taaSharpenPass, false);
    }
    if (debugTaaHistoryPass != backend->end()) {
        m_files.debugTaaHistory = readFiles(*debugTaaHistoryPass, false);
    }
    if (dofCocPass != backend->end()) {
        m_files.dofCoc = readFiles(*dofCocPass, false);
    }
    if (dofGatherPass != backend->end()) {
        m_files.dofGather = readFiles(*dofGatherPass, true);
    }
    if (dofCombinePass != backend->end()) {
        m_files.dofCombine = readFiles(*dofCombinePass, false);
    }
    if (debugDofCocPass != backend->end()) {
        m_files.debugDofCoc = readFiles(*debugDofCocPass, false);
    }
    if (debugFxaaPass != backend->end()) {
        m_files.debugFxaa = readFiles(*debugFxaaPass, false);
    }
    if (motionBlurPass != backend->end()) {
        m_files.motionBlur = readFiles(*motionBlurPass, false);
    }
    m_files.shadow = readFiles(*shadow, true);
    m_files.shadowInstanced = readFiles(*shadowInstanced, true);
    m_files.shadowCull = readCompute(*shadowCull);
    m_files.skinning = readCompute(*skinning);
    if (gpuDrivenCull != backend->end()) {
        m_files.gpuDrivenCull = readCompute(*gpuDrivenCull);
    }
    if (gpuDrivenTransparentSort != backend->end()) {
        m_files.gpuDrivenTransparentSort = readCompute(*gpuDrivenTransparentSort);
    }
    m_files.shadowMoments = readCompute(*shadowMoments);
    m_files.shadowSatHorizontal = readCompute(*shadowSatHorizontal);
    m_files.shadowSatVertical = readCompute(*shadowSatVertical);
    m_files.shadowEvsmBlur = readCompute(*shadowEvsmBlur);
    m_files.shadowEvsmBlurMoments = readCompute(*shadowEvsmBlurMoments);
    m_files.shadowCopyDepth = readFiles(*shadowCopyDepth, false);
    m_files.prepass = readFiles(*prepass, true);
    m_files.motionVectors = readFiles(*motionVectors, false);
    m_files.transparentMotionVectors = readFiles(*transparentMotionVectors, false);
    m_files.cameraMotionVectors = readFiles(*cameraMotionVectors, false);
    m_files.depthPyramidCopy = readFiles(*depthPyramidCopy, false);
    m_files.depthPyramidDownsample = readFiles(*depthPyramidDownsample, false);
    if (depthPyramidCompute != backend->end()) {
        m_files.depthPyramidCompute = readCompute(*depthPyramidCompute);
    }
    if (depthPyramidMaxCompute != backend->end()) {
        m_files.depthPyramidMaxCompute = readCompute(*depthPyramidMaxCompute);
    }
    m_files.sky = readFiles(*sky, false);
    m_files.volumetricFog = readFiles(*volumetricFog, false);
    m_files.volumetricFogComposite = readFiles(*volumetricFogComposite, false);
    m_files.debugLightData = readFiles(*debugLightData, false);
    m_files.debugNormal = readFiles(*debugNormal, false);
    m_files.debugMotionVectors = readFiles(*debugMotionVectors, false);
    m_files.debugDepth = readFiles(*debugDepth, false);
    m_files.debugDepthPyramid = readFiles(*debugDepthPyramid, false);
    m_files.debugLightCluster = readFiles(*debugLightCluster, false);
    m_files.debugShadowAtlas = readFiles(*debugShadowAtlas, false);
    m_files.debugColor = readFiles(*debugColor, false);
    m_files.debugLightmap = readFiles(*debugLightmap, false);
    m_files.debugAlbedo = readFiles(*debugAlbedo, true);
    m_files.debugRoughness = readFiles(*debugRoughness, true);
    m_files.debugGbuffer = readFiles(*debugGbuffer, false);
    m_files.wireframe = readFiles(*wireframe, false);
    if (gbuffer != backend->end()) {
        m_files.gbuffer = readFiles(*gbuffer, true);
    }
    if (gbufferInstanced != backend->end()) {
        m_files.gbufferInstanced = readFiles(*gbufferInstanced, true);
    }
    if (gbufferClear != backend->end()) {
        m_files.gbufferClear = readFiles(*gbufferClear, false);
    }
    m_files.gbufferDecals = readFiles(*gbufferDecals, true);
    m_files.decal = readFiles(*decal, false);
    m_files.decalClear = readFiles(*decalClear, false);
    m_files.deferredLighting = readFiles(*deferredLighting, false);
    m_files.deferredLightingAdvanced = readFiles(*deferredLightingAdvanced, false);
    m_files.deferredLightingAdvancedIms = readFiles(*deferredLightingAdvancedIms, false);
    m_files.deferredLightingVariant = readFiles(*deferredLightingVariant, true);
    m_files.deferredLightingVariantIms = readFiles(*deferredLightingVariantIms, true);
    m_files.deferredLightingVariantCompute = readCompute(*deferredLightingVariantCompute);
    m_files.deferredLightingVariantComputeIms = readCompute(*deferredLightingVariantComputeIms);
    m_files.litMaterialFlags = readCompute(*litMaterialFlags);
    m_files.litVariantIndirectClear = readCompute(*litVariantIndirectClear);
    m_files.litVariantIndirect = readCompute(*litVariantIndirect);
    m_files.ssao = readFiles(*ssao, false);
    m_files.ssaoBlur = readFiles(*ssaoBlur, false);
    m_files.ssaoTemporal = readFiles(*ssaoTemporal, false);
    m_files.ssaoUpsample = readFiles(*ssaoUpsample, false);
    m_files.debugSsao = readFiles(*debugSsao, false);
    m_files.ssrTrace = readFiles(*ssrTrace, false);
    m_files.ssrTemporal = readFiles(*ssrTemporal, false);
    m_files.ssrComposite = readFiles(*ssrComposite, false);
    m_files.debugSsr = readFiles(*debugSsr, false);
    m_files.debugDecals = readFiles(*debugDecals, false);
    m_files.ssgiTrace = readFiles(*ssgiTrace, false);
    m_files.ssgiDenoise = readFiles(*ssgiDenoise, false);
    m_files.ssgiTemporal = readFiles(*ssgiTemporal, false);
    m_files.ssgiComposite = readFiles(*ssgiComposite, false);
    m_files.debugSsgi = readFiles(*debugSsgi, false);
    m_files.debugSsgiRaw = readFiles(*debugSsgiRaw, false);
    m_files.contactShadows = readCompute(*contactShadows);
    if (lightScreenAabb != backend->end()) {
        m_files.lightScreenAabb = readCompute(*lightScreenAabb);
    }
    if (lightBigTile != backend->end()) {
        m_files.lightBigTile = readCompute(*lightBigTile);
    }
    if (lightFptl != backend->end()) {
        m_files.lightFptl = readCompute(*lightFptl);
    }
    if (lightCluster != backend->end()) {
        m_files.lightCluster = readCompute(*lightCluster);
    }
    m_files.lightScreenAabbBuffer = readCompute(*lightScreenAabbBuffer);
    m_files.lightBigTileBuffer = readCompute(*lightBigTileBuffer);
    m_files.lightFptlBuffer = readCompute(*lightFptlBuffer);
    m_files.lightClusterBuffer = readCompute(*lightClusterBuffer);
    m_files.lightIndirectArgs = readCompute(*lightIndirectArgs);
    m_files.debugContactShadows = readFiles(*debugContactShadows, false);
    m_files.debugShadowCascade = readFiles(*debugShadowCascade, false);
    m_files.debugShadowFilter = readFiles(*debugShadowFilter, false);
    m_files.ui = readFiles(*ui, false);
    m_valid = !m_files.forward.vertex.empty() && !m_files.forward.fragments.empty() &&
              !m_files.forwardAdvanced.vertex.empty() && !m_files.forwardAdvanced.fragments.empty() &&
              !m_files.forwardInstanced.vertex.empty() && !m_files.forwardInstanced.fragments.empty() &&
              !m_files.bakedGI.vertex.empty() && !m_files.bakedGI.fragments.empty() &&
              !m_files.bakedGICompensate.vertex.empty() && !m_files.bakedGICompensate.fragments.empty() &&
              !m_files.bakedGIForwardComposite.vertex.empty() &&
              !m_files.bakedGIForwardComposite.fragments.empty() &&
              !m_files.tonemap.vertex.empty() && !m_files.tonemap.fragments.empty() && !m_files.shadow.vertex.empty() &&
              !m_files.shadow.fragments.empty() && !m_files.shadowInstanced.vertex.empty() &&
              !m_files.shadowInstanced.fragments.empty() && !m_files.prepass.vertex.empty() &&
              !m_files.prepass.fragments.empty() && !m_files.motionVectors.vertex.empty() &&
              !m_files.motionVectors.fragments.empty() && !m_files.transparentMotionVectors.vertex.empty() &&
              !m_files.transparentMotionVectors.fragments.empty() &&
              !m_files.cameraMotionVectors.vertex.empty() &&
              !m_files.cameraMotionVectors.fragments.empty() && !m_files.depthPyramidCopy.vertex.empty() &&
              !m_files.depthPyramidCopy.fragments.empty() && !m_files.depthPyramidDownsample.vertex.empty() &&
              !m_files.depthPyramidDownsample.fragments.empty() && !m_files.sky.vertex.empty() &&
              !m_files.sky.fragments.empty() && !m_files.volumetricFog.vertex.empty() &&
              !m_files.volumetricFog.fragments.empty() && !m_files.volumetricFogComposite.vertex.empty() &&
              !m_files.volumetricFogComposite.fragments.empty() && !m_files.debugLightData.vertex.empty() &&
              !m_files.debugLightData.fragments.empty() && !m_files.debugNormal.vertex.empty() &&
              !m_files.debugNormal.fragments.empty() && !m_files.debugMotionVectors.vertex.empty() &&
              !m_files.debugMotionVectors.fragments.empty() && !m_files.debugDepth.vertex.empty() &&
              !m_files.debugDepth.fragments.empty() && !m_files.debugDepthPyramid.vertex.empty() &&
              !m_files.debugDepthPyramid.fragments.empty() && !m_files.debugLightCluster.vertex.empty() &&
              !m_files.debugLightCluster.fragments.empty() && !m_files.debugShadowAtlas.vertex.empty() &&
              !m_files.debugShadowAtlas.fragments.empty() && !m_files.debugColor.vertex.empty() &&
              !m_files.debugColor.fragments.empty() && !m_files.debugLightmap.vertex.empty() &&
              !m_files.debugLightmap.fragments.empty() && !m_files.debugAlbedo.vertex.empty() &&
              !m_files.debugAlbedo.fragments.empty() && !m_files.debugRoughness.vertex.empty() &&
              !m_files.debugRoughness.fragments.empty() && !m_files.debugGbuffer.vertex.empty() &&
              !m_files.debugGbuffer.fragments.empty() && !m_files.wireframe.vertex.empty() &&
              !m_files.wireframe.fragments.empty() && !m_files.gbufferDecals.vertex.empty() &&
              !m_files.gbufferDecals.fragments.empty() && !m_files.decal.vertex.empty() &&
              !m_files.decal.fragments.empty() && !m_files.decalClear.vertex.empty() &&
              !m_files.decalClear.fragments.empty() && !m_files.deferredLighting.vertex.empty() &&
              !m_files.deferredLighting.fragments.empty() &&
              !m_files.deferredLightingAdvanced.vertex.empty() &&
              !m_files.deferredLightingAdvanced.fragments.empty() &&
              !m_files.deferredLightingAdvancedIms.vertex.empty() &&
              !m_files.deferredLightingAdvancedIms.fragments.empty() &&
              !m_files.deferredLightingVariant.vertex.empty() &&
              !m_files.deferredLightingVariant.fragments.empty() &&
              !m_files.deferredLightingVariant.bufferFragments.empty() &&
              !m_files.deferredLightingVariantIms.vertex.empty() &&
              !m_files.deferredLightingVariantIms.fragments.empty() &&
              !m_files.deferredLightingVariantIms.bufferFragments.empty() &&
              !m_files.ssao.vertex.empty() &&
              !m_files.ssao.fragments.empty() && !m_files.ssaoBlur.vertex.empty() &&
              !m_files.ssaoBlur.fragments.empty() && !m_files.ssaoTemporal.vertex.empty() &&
              !m_files.ssaoTemporal.fragments.empty() && !m_files.ssaoUpsample.vertex.empty() &&
              !m_files.ssaoUpsample.fragments.empty() && !m_files.debugSsao.vertex.empty() &&
              !m_files.debugSsao.fragments.empty() && !m_files.ssrTrace.vertex.empty() &&
              !m_files.ssrTrace.fragments.empty() && !m_files.ssrTemporal.vertex.empty() &&
              !m_files.ssrTemporal.fragments.empty() && !m_files.ssrComposite.vertex.empty() &&
              !m_files.ssrComposite.fragments.empty() && !m_files.debugSsr.vertex.empty() &&
              !m_files.debugSsr.fragments.empty() && !m_files.debugDecals.vertex.empty() &&
              !m_files.debugDecals.fragments.empty() && !m_files.ssgiTrace.vertex.empty() &&
              !m_files.ssgiTrace.fragments.empty() && !m_files.ssgiDenoise.vertex.empty() &&
              !m_files.ssgiDenoise.fragments.empty() && !m_files.ssgiTemporal.vertex.empty() &&
              !m_files.ssgiTemporal.fragments.empty() && !m_files.ssgiComposite.vertex.empty() &&
              !m_files.ssgiComposite.fragments.empty() && !m_files.debugSsgi.vertex.empty() &&
              !m_files.debugSsgi.fragments.empty() && !m_files.debugSsgiRaw.vertex.empty() &&
              !m_files.debugSsgiRaw.fragments.empty() && !m_files.contactShadows.compute.empty() &&
               !m_files.shadowCull.compute.empty() && !m_files.shadowMoments.compute.empty() &&
               !m_files.skinning.compute.empty() &&
               !m_files.shadowSatHorizontal.compute.empty() && !m_files.shadowSatVertical.compute.empty() &&
               !m_files.shadowEvsmBlur.compute.empty() && !m_files.shadowEvsmBlurMoments.compute.empty() &&
              !m_files.shadowCopyDepth.vertex.empty() && !m_files.shadowCopyDepth.fragments.empty() &&
              !m_files.debugContactShadows.vertex.empty() && !m_files.debugContactShadows.fragments.empty() &&
              !m_files.debugShadowCascade.vertex.empty() && !m_files.debugShadowCascade.fragments.empty() &&
              !m_files.debugShadowFilter.vertex.empty() && !m_files.debugShadowFilter.fragments.empty() &&
              !m_files.bloomPrefilter.fragments.empty() &&
              !m_files.bloomBlur.fragments.empty() && !m_files.bloomDownsample.fragments.empty() &&
              !m_files.bloomUpsample.fragments.empty() && !m_files.debugBloom.fragments.empty() &&
              !m_files.taa.vertex.empty() && !m_files.taa.fragments.empty() && !m_files.taaSharpen.vertex.empty() &&
              !m_files.taaSharpen.fragments.empty() && !m_files.debugTaaHistory.vertex.empty() &&
              !m_files.debugTaaHistory.fragments.empty() && !m_files.dofCoc.vertex.empty() &&
              !m_files.dofCoc.fragments.empty() && !m_files.dofGather.vertex.empty() &&
              !m_files.dofGather.fragments.empty() && !m_files.dofCombine.vertex.empty() &&
              !m_files.dofCombine.fragments.empty() && !m_files.debugDofCoc.vertex.empty() &&
              !m_files.debugDofCoc.fragments.empty() && !m_files.debugFxaa.vertex.empty() &&
              !m_files.debugFxaa.fragments.empty() && !m_files.motionBlur.vertex.empty() &&
              !m_files.motionBlur.fragments.empty() &&
              !m_files.ui.vertex.empty() && !m_files.ui.fragments.empty();
    if (!m_valid) {
        HBG_LOGE("shader manifest backend '{}' has invalid pass entries", m_backend);
    }
    return m_valid;
}

gfx::ProgramHandle ShaderLibrary::createProgram(const PassFiles& files, uint32_t variant, bool bufferPath,
                                                             bool lightLayers) {
    if (m_resources == nullptr) {
        return {};
    }
    const std::vector<std::string>& fragments = bufferPath ? files.bufferFragments : files.fragments;
    if (lightLayers) {
        variant += kMaterialVariantCount;
    }
    if (variant >= fragments.size()) {
        if (bufferPath) {
            HBG_LOGW("buffer light-list shader variant {} is not compiled", variant);
            return {};
        }
        HBG_LOGW("shader variant {} is not compiled; falling back to variant 0", variant);
        variant = 0;
    }
    if (variant >= fragments.size()) {
        return {};
    }

    const std::filesystem::path vertexPath = m_root / files.vertex;
    const std::filesystem::path fragmentPath = m_root / fragments[variant];
    const std::string name = fragmentPath.stem().string();
    gfx::ProgramHandle program = m_resources->createProgramFromFiles(vertexPath.string(), fragmentPath.string(), name);
    if (!program.valid()) {
        HBG_LOGE("failed to create shader program '{}' (vertex='{}')", name, vertexPath.string());
    }
    return program;
}

gfx::ProgramHandle ShaderLibrary::createComputeProgram(const PassFiles& files, bool bufferPath,
                                                       uint32_t variant) {
    const std::vector<std::string>& computes = bufferPath ? files.bufferComputes : files.computes;
    std::string computeFile = bufferPath ? files.bufferCompute : files.compute;
    if (!computes.empty()) {
        if (variant >= computes.size()) {
            return {};
        }
        computeFile = computes[variant];
    }
    if (m_resources == nullptr || computeFile.empty()) {
        return {};
    }

    const std::filesystem::path computePath = m_root / computeFile;
    const std::string name = computePath.stem().string();
    gfx::ProgramHandle program = m_resources->createComputeProgramFromFiles(computePath.string(), name);
    if (!program.valid()) {
        HBG_LOGE("failed to create compute shader program '{}'", computePath.string());
    }
    return program;
}

gfx::ProgramHandle ShaderLibrary::program(PassId pass, uint64_t materialKey, uint64_t pipelineKey) {
    if (!m_valid) {
        return {};
    }

    const bool bufferPath = (pipelineKey & kLightListBufferProgramKey) != 0;
    const bool lightLayersPath = (pipelineKey & kLightLayersProgramKey) != 0;
    uint32_t variant = static_cast<uint32_t>(materialKey) & kMaterialVariantMask;
    const PassFiles* files = &m_files.forward;
    switch (pass) {
    case PassId::Forward:
        break;
    case PassId::ForwardAdvanced:
        files = &m_files.forwardAdvanced;
        variant = ((materialKey & MaterialVariantDoubleSided) != 0 ? 1u : 0u) |
                  ((materialKey & (MaterialVariantNormalMap | MaterialVariantMaskMap)) != 0 ? 2u : 0u);
        break;
    case PassId::ForwardInstanced:
        files = &m_files.forwardInstanced;
        break;
    case PassId::BindlessForwardInstanced:
        files = &m_files.bindlessForward;
        break;
    case PassId::BakedGI:
        files = &m_files.bakedGI;
        break;
    case PassId::BakedGICompensate:
        files = &m_files.bakedGICompensate;
        variant = 0;
        break;
    case PassId::BakedGIForwardComposite:
        files = &m_files.bakedGIForwardComposite;
        variant = 0;
        break;
    case PassId::Tonemap:
        files = &m_files.tonemap;
        variant = 0;
        break;
    case PassId::PostProcessExposure:
        files = &m_files.postProcessExposure;
        variant = 0;
        break;
    case PassId::PostProcessExposurePrepass:
        files = &m_files.postProcessExposurePrepass;
        variant = 0;
        break;
    case PassId::PostProcessExposureReduce:
        files = &m_files.postProcessExposureReduce;
        variant = 0;
        break;
    case PassId::PostProcessExposureReduceFinal:
        files = &m_files.postProcessExposureReduceFinal;
        variant = 0;
        break;
    case PassId::PostProcessHistogramClear:
        files = &m_files.postProcessHistogramClear;
        variant = 0;
        break;
    case PassId::PostProcessHistogramGen:
        files = &m_files.postProcessHistogramGen;
        variant = 0;
        break;
    case PassId::PostProcessHistogramReduce:
        files = &m_files.postProcessHistogramReduce;
        variant = 0;
        break;
    case PassId::ApplyExposure:
        files = &m_files.applyExposure;
        variant = 0;
        break;
    case PassId::DebugExposure:
        files = &m_files.debugExposure;
        variant = 0;
        break;
    case PassId::Final:
        files = &m_files.final;
        break;
    case PassId::UberPost:
        files = &m_files.uberPost;
        variant = 0;
        break;
    case PassId::DebugColorGrading:
        files = &m_files.debugColorGrading;
        variant = 0;
        break;
    case PassId::BloomPrefilter:
        files = &m_files.bloomPrefilter;
        break;
    case PassId::BloomBlur:
        files = &m_files.bloomBlur;
        break;
    case PassId::BloomDownsample:
        files = &m_files.bloomDownsample;
        variant = 0;
        break;
    case PassId::BloomUpsample:
        files = &m_files.bloomUpsample;
        break;
    case PassId::DebugBloom:
        files = &m_files.debugBloom;
        variant = 0;
        break;
    case PassId::Taa:
        files = &m_files.taa;
        variant = 0;
        break;
    case PassId::TaaSharpen:
        files = &m_files.taaSharpen;
        variant = 0;
        break;
    case PassId::DebugTaaHistory:
        files = &m_files.debugTaaHistory;
        variant = 0;
        break;
    case PassId::DofCoc:
        files = &m_files.dofCoc;
        variant = 0;
        break;
    case PassId::DofGather:
        files = &m_files.dofGather;
        variant = variant > 1u ? 0u : variant;
        break;
    case PassId::DofCombine:
        files = &m_files.dofCombine;
        variant = 0;
        break;
    case PassId::DebugDofCoc:
        files = &m_files.debugDofCoc;
        variant = 0;
        break;
    case PassId::DebugFxaa:
        files = &m_files.debugFxaa;
        variant = 0;
        break;
    case PassId::MotionBlur:
        files = &m_files.motionBlur;
        variant = 0;
        break;
    case PassId::Shadow:
        files = &m_files.shadow;
        break;
    case PassId::ShadowInstanced:
        files = &m_files.shadowInstanced;
        break;
    case PassId::ShadowCull:
        files = &m_files.shadowCull;
        variant = 0;
        break;
    case PassId::GpuDrivenCull:
        files = &m_files.gpuDrivenCull;
        variant = 0;
        break;
    case PassId::GpuDrivenTransparentSort:
        files = &m_files.gpuDrivenTransparentSort;
        variant = 0;
        break;
    case PassId::Skinning:
        files = &m_files.skinning;
        variant = 0;
        break;
    case PassId::ShadowMoments:
        files = &m_files.shadowMoments;
        variant = 0;
        break;
    case PassId::ShadowSatHorizontal:
        files = &m_files.shadowSatHorizontal;
        variant = 0;
        break;
    case PassId::ShadowSatVertical:
        files = &m_files.shadowSatVertical;
        variant = 0;
        break;
    case PassId::ShadowEvsmBlur:
        files = &m_files.shadowEvsmBlur;
        variant = 0;
        break;
    case PassId::ShadowEvsmBlurMoments:
        files = &m_files.shadowEvsmBlurMoments;
        variant = 0;
        break;
    case PassId::ShadowCopyDepth:
        files = &m_files.shadowCopyDepth;
        variant = 0;
        break;
    case PassId::Prepass:
        files = &m_files.prepass;
        break;
    case PassId::MotionVectors:
        files = &m_files.motionVectors;
        variant = 0;
        break;
    case PassId::TransparentMotionVectors:
        files = &m_files.transparentMotionVectors;
        variant = 0;
        break;
    case PassId::CameraMotionVectors:
        files = &m_files.cameraMotionVectors;
        variant = 0;
        break;
    case PassId::DepthPyramidCopy:
        files = &m_files.depthPyramidCopy;
        variant = 0;
        break;
    case PassId::DepthPyramidDownsample:
        files = &m_files.depthPyramidDownsample;
        variant = 0;
        break;
    case PassId::DepthPyramidCompute:
        files = &m_files.depthPyramidCompute;
        variant = 0;
        break;
    case PassId::DepthPyramidMaxCompute:
        files = &m_files.depthPyramidMaxCompute;
        variant = 0;
        break;
    case PassId::Sky:
        files = &m_files.sky;
        variant = 0;
        break;
    case PassId::VolumetricFog:
        files = &m_files.volumetricFog;
        variant = 0;
        break;
    case PassId::VolumetricFogComposite:
        files = &m_files.volumetricFogComposite;
        variant = 0;
        break;
    case PassId::DebugLightData:
        files = &m_files.debugLightData;
        variant = 0;
        break;
    case PassId::DebugNormal:
        files = &m_files.debugNormal;
        variant = 0;
        break;
    case PassId::DebugMotionVectors:
        files = &m_files.debugMotionVectors;
        variant = 0;
        break;
    case PassId::DebugDepth:
        files = &m_files.debugDepth;
        variant = 0;
        break;
    case PassId::DebugDepthPyramid:
        files = &m_files.debugDepthPyramid;
        variant = 0;
        break;
    case PassId::DebugLightCluster:
        files = &m_files.debugLightCluster;
        variant = 0;
        break;
    case PassId::DebugShadowAtlas:
        files = &m_files.debugShadowAtlas;
        variant = 0;
        break;
    case PassId::DebugColor:
        files = &m_files.debugColor;
        variant = 0;
        break;
    case PassId::DebugLightmap:
        files = &m_files.debugLightmap;
        variant = 0;
        break;
    case PassId::DebugAlbedo:
        files = &m_files.debugAlbedo;
        break;
    case PassId::DebugRoughness:
        files = &m_files.debugRoughness;
        break;
    case PassId::DebugGBuffer:
        files = &m_files.debugGbuffer;
        variant = 0;
        break;
    case PassId::Wireframe:
        files = &m_files.wireframe;
        variant = 0;
        break;
    case PassId::GBuffer:
        files = &m_files.gbuffer;
        break;
    case PassId::GBufferInstanced:
        files = &m_files.gbufferInstanced;
        break;
    case PassId::GBufferClear:
        files = &m_files.gbufferClear;
        variant = 0;
        break;
    case PassId::GBufferDecals:
        files = &m_files.gbufferDecals;
        break;
    case PassId::Decal:
        files = &m_files.decal;
        variant = 0;
        break;
    case PassId::DecalClear:
        files = &m_files.decalClear;
        variant = 0;
        break;
    case PassId::DeferredLighting:
        files = &m_files.deferredLighting;
        variant = 0;
        break;
    case PassId::DeferredLightingAdvanced:
        files = &m_files.deferredLightingAdvanced;
        variant = 0;
        break;
    case PassId::DeferredLightingAdvancedIms:
        files = &m_files.deferredLightingAdvancedIms;
        variant = 0;
        break;
    case PassId::DeferredLightingVariant:
        files = &m_files.deferredLightingVariant;
        variant = static_cast<uint32_t>(materialKey) % kDeferredLightingVariantCount;
        break;
    case PassId::DeferredLightingVariantIms:
        files = &m_files.deferredLightingVariantIms;
        variant = static_cast<uint32_t>(materialKey) % kDeferredLightingVariantCount;
        break;
    case PassId::DeferredLightingVariantCompute:
        files = &m_files.deferredLightingVariantCompute;
        variant = static_cast<uint32_t>(materialKey) % kDeferredLightingVariantCount;
        break;
    case PassId::DeferredLightingVariantComputeIms:
        files = &m_files.deferredLightingVariantComputeIms;
        variant = static_cast<uint32_t>(materialKey) % kDeferredLightingVariantCount;
        break;
    case PassId::LitMaterialFlags:
        files = &m_files.litMaterialFlags;
        variant = 0;
        break;
    case PassId::LitVariantIndirectClear:
        files = &m_files.litVariantIndirectClear;
        variant = 0;
        break;
    case PassId::LitVariantIndirect:
        files = &m_files.litVariantIndirect;
        variant = 0;
        break;
    case PassId::SSAO:
        files = &m_files.ssao;
        variant = 0;
        break;
    case PassId::SSAOBlur:
        files = &m_files.ssaoBlur;
        variant = 0;
        break;
    case PassId::SSAOTemporal:
        files = &m_files.ssaoTemporal;
        variant = 0;
        break;
    case PassId::SSAOUpsample:
        files = &m_files.ssaoUpsample;
        variant = 0;
        break;
    case PassId::DebugSSAO:
        files = &m_files.debugSsao;
        variant = 0;
        break;
    case PassId::SSR:
        files = &m_files.ssrTrace;
        variant = 0;
        break;
    case PassId::SSRTemporal:
        files = &m_files.ssrTemporal;
        variant = 0;
        break;
    case PassId::SSRComposite:
        files = &m_files.ssrComposite;
        variant = 0;
        break;
    case PassId::DebugSSR:
        files = &m_files.debugSsr;
        variant = 0;
        break;
    case PassId::DebugDecals:
        files = &m_files.debugDecals;
        variant = 0;
        break;
    case PassId::SSGI:
        files = &m_files.ssgiTrace;
        variant = 0;
        break;
    case PassId::SSGIDenoise:
        files = &m_files.ssgiDenoise;
        variant = 0;
        break;
    case PassId::SSGITemporal:
        files = &m_files.ssgiTemporal;
        variant = 0;
        break;
    case PassId::SSGIComposite:
        files = &m_files.ssgiComposite;
        variant = 0;
        break;
    case PassId::DebugSSGI:
        files = &m_files.debugSsgi;
        variant = 0;
        break;
    case PassId::DebugSSGIRaw:
        files = &m_files.debugSsgiRaw;
        variant = 0;
        break;
    case PassId::ContactShadows:
        files = &m_files.contactShadows;
        variant = 0;
        break;
    case PassId::LightScreenAABB:
        files = &m_files.lightScreenAabb;
        variant = 0;
        break;
    case PassId::LightBigTile:
        files = &m_files.lightBigTile;
        variant = 0;
        break;
    case PassId::LightFptl:
        files = &m_files.lightFptl;
        variant = 0;
        break;
    case PassId::LightCluster:
        files = &m_files.lightCluster;
        variant = 0;
        break;
    case PassId::LightScreenAABBBuffer:
        files = &m_files.lightScreenAabbBuffer;
        variant = 0;
        break;
    case PassId::LightBigTileBuffer:
        files = &m_files.lightBigTileBuffer;
        variant = 0;
        break;
    case PassId::LightFptlBuffer:
        files = &m_files.lightFptlBuffer;
        variant = 0;
        break;
    case PassId::LightClusterBuffer:
        files = &m_files.lightClusterBuffer;
        variant = 0;
        break;
    case PassId::LightIndirectArgs:
        files = &m_files.lightIndirectArgs;
        variant = 0;
        break;
    case PassId::DebugContactShadows:
        files = &m_files.debugContactShadows;
        variant = 0;
        break;
    case PassId::DebugShadowCascade:
        files = &m_files.debugShadowCascade;
        variant = 0;
        break;
    case PassId::DebugShadowFilter:
        files = &m_files.debugShadowFilter;
        variant = 0;
        break;
    case PassId::Ui:
        files = &m_files.ui;
        variant = 0;
        break;
    }

    const uint64_t key = cacheKey(pass, variant,
                                    (bufferPath ? kLightListBufferProgramKey : 0) |
                                        (lightLayersPath ? kLightLayersProgramKey : 0));
    const auto cached = m_programs.find(key);
    if (cached != m_programs.end()) {
        return cached->second;
    }

    gfx::ProgramHandle program;
    if (!files->compute.empty() || !files->bufferCompute.empty() || !files->computes.empty() ||
        !files->bufferComputes.empty()) {
        program = createComputeProgram(*files, bufferPath, variant);
    } else {
        program = createProgram(*files, variant, bufferPath, lightLayersPath);
        if (!program.valid() && variant != 0 && !bufferPath) {
            HBG_LOGW("falling back to shader variant 0 for pass {}", static_cast<int>(pass));
            program = createProgram(*files, 0, false, lightLayersPath);
        }
    }
    if (program.valid()) {
        m_programs.emplace(key, program);
    }
    return program;
}

void ShaderLibrary::shutdown(gfx::ResourceManager& resources) {
    for (const auto& [key, program] : m_programs) {
        (void)key;
        if (program.valid()) {
            resources.destroyProgram(program);
        }
    }
    m_programs.clear();
    m_valid = false;
    m_resources = nullptr;
}

} // namespace hbg::shader
