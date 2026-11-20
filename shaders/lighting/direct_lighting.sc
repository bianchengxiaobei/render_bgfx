#ifndef HBG_DIRECT_LIGHTING_SC
#define HBG_DIRECT_LIGHTING_SC

#include "common/common.sc"
#include "common/brdf.sc"
#include "material/surface_data_types.sc"
#include "lighting/environment.sc"

struct LitResult
{
    vec3 diffuse;
    vec3 specular;
};

#ifndef HBG_VARIANT_MATERIAL_FLAGS
#ifdef HBG_ADVANCED_MATERIAL
#define HBG_VARIANT_MATERIAL_FLAGS 0xffffu
#else
#define HBG_VARIANT_MATERIAL_FLAGS 1u
#endif
#endif

#ifdef HBG_ADVANCED_MATERIAL
#include "lighting/direct_lighting_advanced.sc"
#endif

float hbgComputeMicroShadowing(float ambientOcclusion, float NdotL, float opacity)
{
    const float aperture = 2.0 * ambientOcclusion * ambientOcclusion;
    return mix(1.0, hbgSaturate(NdotL + aperture - 1.0), opacity);
}

vec3 hbgComputeShadowColor(float shadow, vec3 shadowTint, float penumbraTint)
{
    const vec3 invTint = vec3(1.0, 1.0, 1.0) - shadowTint;
    const float shadow3 = shadow * shadow * shadow;
    return mix(vec3(1.0, 1.0, 1.0) - ((1.0 - shadow) * invTint),
               shadow3 * invTint + shadow * shadowTint, penumbraTint);
}

LitResult hbgEvaluateLight(SurfaceData surfaceData, vec3 viewWS, vec3 lightDirectionWS, vec3 radiance,
                           vec3 visibility)
{
#ifdef HBG_ADVANCED_MATERIAL
    return hbgEvaluateLightAdvanced(surfaceData, viewWS, lightDirectionWS, radiance, visibility);
#endif
    LitResult result;
    result.diffuse = vec3(0.0, 0.0, 0.0);
    result.specular = vec3(0.0, 0.0, 0.0);

    vec3 L = normalize(lightDirectionWS);
    vec3 V = normalize(viewWS);
    vec3 N = normalize(surfaceData.normalWS);
    vec3 H = normalize(L + V);

    float NdotL = dot(N, L);
    float NdotV = max(dot(N, V), 1.0e-4);
    float NdotH = hbgSaturate(dot(N, H));
    float LdotH = hbgSaturate(dot(L, H));
    float LdotV = dot(L, V);

    if (NdotL <= 0.0 || max(visibility.r, max(visibility.g, visibility.b)) <= 0.0)
    {
        return result;
    }

    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), surfaceData.baseColor, surfaceData.metallic);
    vec3 diffuseColor = surfaceData.baseColor * (1.0 - surfaceData.metallic);
    vec3 fresnel = hbgF_Schlick(f0, LdotH);
    float D = hbgD_GGX(NdotH, surfaceData.roughness);
    float Vis = hbgV_SmithJointGGX(NdotL, NdotV, surfaceData.roughness);

    float diffuseTerm = hbgDisneyDiffuse(NdotV, NdotL, LdotV, surfaceData.perceptualRoughness);
    float specularTerm = D * Vis;

    result.diffuse = diffuseColor * diffuseTerm * NdotL * radiance * visibility;
    result.specular = fresnel * specularTerm * NdotL * radiance * visibility;
    return result;
}

vec3 hbgEvaluateAmbient(SurfaceData surfaceData, vec3 viewWS, vec3 worldPosition, vec3 ambientColor,
                        float ambientIntensity)
{
    vec3 N = normalize(surfaceData.normalWS);
    vec3 V = normalize(viewWS);
#if (HBG_VARIANT_MATERIAL_FLAGS & 32)
    vec3 f0 = hbgAdvancedFresnel0(surfaceData, max(dot(N, V), 1.0e-4));
#else
    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), surfaceData.baseColor, surfaceData.metallic);
#endif
    float NdotV = max(dot(N, V), 1.0e-4);

#ifdef HBG_FORWARD_LIGHTMAP_GI
    const bool bakedLightmap = u_pass_lightmapParams2.w > 0.5 && u_object_lightmapParams.x > 0.5;
#else
    const bool bakedLightmap = false;
#endif

    vec3 result = vec3(0.0, 0.0, 0.0);
    if (!bakedLightmap)
    {
        const vec3 fresnel = hbgF_Schlick(f0, NdotV);
        const vec3 skyColor = mix(vec3(0.25, 0.30, 0.40), vec3(0.75, 0.82, 1.0),
                                  hbgSaturate(N.y * 0.5 + 0.5));
        result = (surfaceData.baseColor * (1.0 - surfaceData.metallic) * surfaceData.ambientOcclusion +
                  fresnel * skyColor * (1.0 - surfaceData.roughness * 0.5) *
                      surfaceData.ambientOcclusion) *
                 ambientColor * ambientIntensity;
    }

    const bool skyIblEnabled = u_environment_skyParams.z > 0.5;
    if (skyIblEnabled && !bakedLightmap)
    {
        const float intensity = u_environment_skyParams.x;
        result = surfaceData.baseColor * (1.0 - surfaceData.metallic) * surfaceData.ambientOcclusion *
                 hbgSampleSH9(N) * intensity;
#if (HBG_VARIANT_MATERIAL_FLAGS & 4)
        result += surfaceData.baseColor * hbgSaturate(surfaceData.subsurfaceMask) *
                  hbgSampleSH9(N) * intensity * 0.25;
#endif
    }

    if (skyIblEnabled || u_probe_global.x > 0.5)
    {
        const float perceptualRoughness = surfaceData.perceptualRoughness;
        const vec3 reflection = reflect(-V, N);
        const vec2 ab = hbgEnvBRDFApprox(NdotV, perceptualRoughness);
        float probeWeight = 0.0;
        const vec3 environmentReflection =
            hbgSampleReflection(worldPosition, reflection, perceptualRoughness, probeWeight);
#if (HBG_VARIANT_MATERIAL_FLAGS & 64)
        const float coatMask = hbgSaturate(surfaceData.clearCoatMask);
        result += environmentReflection * (f0 * ab.x + ab.y) * surfaceData.ambientOcclusion *
                  (1.0 - coatMask * 0.5);
        if (coatMask > 0.0)
        {
            const float coatRoughness = clamp(surfaceData.clearCoatRoughness, 0.002, 1.0);
            const vec2 coatAb = hbgEnvBRDFApprox(NdotV, coatRoughness);
            float coatProbeWeight = 0.0;
            const vec3 coatReflection = hbgSampleReflection(worldPosition, reflection, coatRoughness,
                                                             coatProbeWeight);
            result += coatReflection * (vec3(0.04, 0.04, 0.04) * coatAb.x + coatAb.y) *
                      coatMask * surfaceData.ambientOcclusion;
        }
#else
        result += environmentReflection * (f0 * ab.x + ab.y) * surfaceData.ambientOcclusion;
#endif
    }
    return result;
}

#endif // HBG_DIRECT_LIGHTING_SC
