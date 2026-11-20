#ifndef HBG_DIRECT_LIGHTING_ADVANCED_SC
#define HBG_DIRECT_LIGHTING_ADVANCED_SC

#include "common/common.sc"
#include "common/brdf.sc"
#include "common/brdf_advanced.sc"
#include "material/surface_data_types.sc"

vec3 hbgAdvancedTangent(SurfaceData surfaceData)
{
    vec3 N = normalize(surfaceData.normalWS);
    vec3 tangent = surfaceData.tangentWS;
    if (dot(tangent, tangent) < 1.0e-8)
    {
        const vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        tangent = normalize(cross(up, N));
    }
    tangent = normalize(tangent - N * dot(N, tangent));
    const float angle = surfaceData.anisotropyRotation;
    const float c = cos(angle);
    const float s = sin(angle);
    return normalize(tangent * c + cross(N, tangent) * s);
}

vec3 hbgAdvancedFresnel0(SurfaceData surfaceData, float NdotV)
{
    vec3 f0 = mix(vec3(0.04, 0.04, 0.04), surfaceData.baseColor, surfaceData.metallic);
#if (HBG_VARIANT_MATERIAL_FLAGS & 32)
    if (surfaceData.iridescenceMask > 0.0)
    {
        const vec3 iridescence = hbgEvalIridescence(surfaceData.ior, NdotV,
                                                     surfaceData.iridescenceThickness * 0.001,
                                                     f0);
        f0 = mix(f0, iridescence, hbgSaturate(surfaceData.iridescenceMask));
    }
#endif
    return f0;
}

LitResult hbgEvaluateLightAdvanced(SurfaceData surfaceData, vec3 viewWS, vec3 lightDirectionWS,
                                    vec3 radiance, vec3 visibility)
{
    LitResult result;
    result.diffuse = vec3(0.0, 0.0, 0.0);
    result.specular = vec3(0.0, 0.0, 0.0);

    const vec3 L = normalize(lightDirectionWS);
    const vec3 V = normalize(viewWS);
    const vec3 N = normalize(surfaceData.normalWS);
    const vec3 H = normalize(L + V);
    const float NdotL = dot(N, L);
    const float NdotV = max(dot(N, V), 1.0e-4);
    const float NdotH = hbgSaturate(dot(N, H));
    const float LdotH = hbgSaturate(dot(L, H));
    const float LdotV = dot(L, V);
    const float maxVisibility = max(visibility.r, max(visibility.g, visibility.b));
    float transmissionMask = 0.0;
    float subsurfaceMask = 0.0;
#if (HBG_VARIANT_MATERIAL_FLAGS & 8)
    transmissionMask = hbgSaturate(surfaceData.transmissionMask);
#endif
#if (HBG_VARIANT_MATERIAL_FLAGS & 4)
    subsurfaceMask = hbgSaturate(surfaceData.subsurfaceMask);
#endif
    if (maxVisibility <= 0.0 || (NdotL <= 0.0 && transmissionMask <= 0.0 && subsurfaceMask <= 0.0))
    {
        return result;
    }

    const vec3 f0 = hbgAdvancedFresnel0(surfaceData, NdotV);
    const vec3 diffuseColor = surfaceData.baseColor * (1.0 - surfaceData.metallic);
#if (HBG_VARIANT_MATERIAL_FLAGS & 16)
    const float anisotropy = clamp(surfaceData.anisotropy, -1.0, 1.0);
#endif
#if (HBG_VARIANT_MATERIAL_FLAGS & 64)
    const float coatMask = hbgSaturate(surfaceData.clearCoatMask);
#endif


    if (NdotL > 0.0)
    {
        const vec3 fresnel = hbgF_Schlick(f0, LdotH);
        float specularTerm = 0.0;
#if (HBG_VARIANT_MATERIAL_FLAGS & 16)
        if (abs(anisotropy) > 0.001)
        {
            const vec3 T = hbgAdvancedTangent(surfaceData);
            const vec3 B = cross(N, T);
            const float aspect = sqrt(max(1.0 - 0.9 * abs(anisotropy), 0.001));
            const float roughnessT = max(surfaceData.roughness * surfaceData.roughness *
                                             (1.0 + anisotropy) / aspect, 0.002);
            const float roughnessB = max(surfaceData.roughness * surfaceData.roughness *
                                             (1.0 - anisotropy) * aspect, 0.002);
            specularTerm = hbgDV_SmithJointGGXAniso(dot(T, H), dot(B, H), NdotH, dot(T, V),
                                                   dot(B, V), NdotV, dot(T, L), dot(B, L), NdotL,
                                                   roughnessT, roughnessB);
        }
        else
        {
            specularTerm = hbgD_GGX(NdotH, surfaceData.roughness) *
                           hbgV_SmithJointGGX(NdotL, NdotV, surfaceData.roughness);
        }
#else
        specularTerm = hbgD_GGX(NdotH, surfaceData.roughness) *
                       hbgV_SmithJointGGX(NdotL, NdotV, surfaceData.roughness);
#endif
        const float diffuseTerm = hbgDisneyDiffuse(NdotV, NdotL, LdotV, surfaceData.perceptualRoughness);
        result.diffuse = diffuseColor * diffuseTerm * NdotL * radiance * visibility;
        result.specular = fresnel * specularTerm * NdotL * radiance * visibility;

#if (HBG_VARIANT_MATERIAL_FLAGS & 4)
        if (subsurfaceMask > 0.0)
        {
            const float wrap = hbgSaturate((NdotL + 0.5) / 1.5);
            result.diffuse += surfaceData.baseColor * subsurfaceMask * wrap * radiance * visibility * 0.35;
        }
#endif

#if (HBG_VARIANT_MATERIAL_FLAGS & 64)
        if (coatMask > 0.0)
        {
            const float coatRoughness = clamp(surfaceData.clearCoatRoughness, 0.002, 1.0);
            const float coatD = hbgD_GGX(NdotH, coatRoughness);
            const float coatV = hbgV_SmithJointGGX(NdotL, NdotV, coatRoughness);
            const float coatFresnel = 0.04 + 0.96 * pow(1.0 - LdotH, 5.0);
            const float coatSpecular = coatD * coatV * coatFresnel;
            const float attenuation = 1.0 - coatMask * coatFresnel * 0.5;
            result.diffuse *= attenuation;
            result.specular *= attenuation;
            result.specular += radiance * visibility * NdotL * coatMask * coatSpecular;
        }
#endif
    }

#if (HBG_VARIANT_MATERIAL_FLAGS & 8)
    if (transmissionMask > 0.0)
    {
        const float backLight = hbgSaturate(dot(-N, L));
        const float transmittance = exp(-max(surfaceData.thickness, 0.0) * 0.15);
        result.specular += surfaceData.baseColor * transmissionMask * backLight * transmittance *
                           radiance * visibility * 0.5;
    }
#endif

    return result;
}

#endif // HBG_DIRECT_LIGHTING_ADVANCED_SC
