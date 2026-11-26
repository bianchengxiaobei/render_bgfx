#ifndef HBG_SURFACE_DATA_BINDLESS_SC
#define HBG_SURFACE_DATA_BINDLESS_SC

#include "common/common.sc"
#include "common/packing.sc"
#include "common/brdf.sc"
#include "material/material_bindless.sc"
#include "material/surface_data_types.sc"

SAMPLER2DARRAY(s_baseColorMap, 0);
SAMPLER2DARRAY(s_metallicRoughnessMap, 1);
SAMPLER2DARRAY(s_normalMap, 2);
SAMPLER2DARRAY(s_occlusionMap, 3);
SAMPLER2DARRAY(s_emissiveMap, 4);

vec2 hbgBindlessWrapUv(vec2 uv, int wrapU, int wrapV)
{
    if (wrapU == 0)
    {
        uv.x = fract(uv.x);
    }
    else if (wrapU == 1)
    {
        uv.x = clamp(uv.x, 0.0, 1.0);
    }
    else
    {
        float wrapped = mod(uv.x, 2.0);
        uv.x = wrapped <= 1.0 ? wrapped : 2.0 - wrapped;
    }
    if (wrapV == 0)
    {
        uv.y = fract(uv.y);
    }
    else if (wrapV == 1)
    {
        uv.y = clamp(uv.y, 0.0, 1.0);
    }
    else
    {
        float wrapped = mod(uv.y, 2.0);
        uv.y = wrapped <= 1.0 ? wrapped : 2.0 - wrapped;
    }
    return uv;
}

vec4 hbgMaterialUvTransform(int materialIndex, int channel)
{
    if (channel == 0)
    {
        return hbgMaterialUvScale0(materialIndex);
    }
    if (channel == 1)
    {
        return hbgMaterialUvScale1(materialIndex);
    }
    if (channel == 2)
    {
        return hbgMaterialUvScale2(materialIndex);
    }
    if (channel == 3)
    {
        return hbgMaterialUvScale3(materialIndex);
    }
    return hbgMaterialUvScale4(materialIndex);
}

vec2 hbgMaterialUv(vec2 uv, int materialIndex, int channel)
{
    const vec4 flags = hbgMaterialBindlessFlags(materialIndex);
    if (flags.z < 0.5)
    {
        return uv;
    }
    const int wrapBits = int(flags.y + 0.5);
    const int bits = (wrapBits >> (channel * 4)) & 15;
    uv = hbgBindlessWrapUv(uv, bits & 3, (bits >> 2) & 3);
    const vec4 scaleOffset = hbgMaterialUvTransform(materialIndex, channel);
    return uv * scaleOffset.xy + scaleOffset.zw;
}

#if BGFX_SHADER_LANGUAGE_GLSL
vec4 hbgTexture2DArrayGrad(sampler2DArray textureArray, vec3 coord, vec2 dPdx, vec2 dPdy)
{
    return textureGrad(textureArray, coord, dPdx, dPdy);
}
#else
vec4 hbgTexture2DArrayGrad(BgfxSampler2DArray textureArray, vec3 coord, vec2 dPdx, vec2 dPdy)
{
    return textureArray.m_texture.SampleGrad(textureArray.m_sampler, coord, dPdx, dPdy);
}
#endif

vec4 hbgSampleMaterialBaseColor(vec2 uv, int materialIndex)
{
    const vec3 coord = vec3(hbgMaterialUv(uv, materialIndex, 0), float(materialIndex));
    if (hbgMaterialBindlessFlags(materialIndex).z < 0.5)
    {
        return texture2DArray(s_baseColorMap, coord);
    }
    const vec4 transform = hbgMaterialUvTransform(materialIndex, 0);
    return hbgTexture2DArrayGrad(s_baseColorMap, coord, dFdx(uv) * transform.xy, dFdy(uv) * transform.xy);
}

vec4 hbgSampleMaterialMetallicRoughness(vec2 uv, int materialIndex)
{
    const vec3 coord = vec3(hbgMaterialUv(uv, materialIndex, 1), float(materialIndex));
    if (hbgMaterialBindlessFlags(materialIndex).z < 0.5)
    {
        return texture2DArray(s_metallicRoughnessMap, coord);
    }
    const vec4 transform = hbgMaterialUvTransform(materialIndex, 1);
    return hbgTexture2DArrayGrad(s_metallicRoughnessMap, coord, dFdx(uv) * transform.xy, dFdy(uv) * transform.xy);
}

vec4 hbgSampleMaterialNormal(vec2 uv, int materialIndex)
{
    const vec3 coord = vec3(hbgMaterialUv(uv, materialIndex, 2), float(materialIndex));
    if (hbgMaterialBindlessFlags(materialIndex).z < 0.5)
    {
        return texture2DArray(s_normalMap, coord);
    }
    const vec4 transform = hbgMaterialUvTransform(materialIndex, 2);
    return hbgTexture2DArrayGrad(s_normalMap, coord, dFdx(uv) * transform.xy, dFdy(uv) * transform.xy);
}

vec4 hbgSampleMaterialOcclusion(vec2 uv, int materialIndex)
{
    const vec3 coord = vec3(hbgMaterialUv(uv, materialIndex, 3), float(materialIndex));
    if (hbgMaterialBindlessFlags(materialIndex).z < 0.5)
    {
        return texture2DArray(s_occlusionMap, coord);
    }
    const vec4 transform = hbgMaterialUvTransform(materialIndex, 3);
    return hbgTexture2DArrayGrad(s_occlusionMap, coord, dFdx(uv) * transform.xy, dFdy(uv) * transform.xy);
}

vec3 hbgSampleMaterialEmissive(vec2 uv, int materialIndex)
{
    const vec3 coord = vec3(hbgMaterialUv(uv, materialIndex, 4), float(materialIndex));
    if (hbgMaterialBindlessFlags(materialIndex).z < 0.5)
    {
        return texture2DArray(s_emissiveMap, coord).rgb;
    }
    const vec4 transform = hbgMaterialUvTransform(materialIndex, 4);
    return hbgTexture2DArrayGrad(s_emissiveMap, coord, dFdx(uv) * transform.xy, dFdy(uv) * transform.xy).rgb;
}

SurfaceData hbgMakeSurfaceData(vec3 worldPos, vec3 geometricNormalWS, vec3 tangentWS, vec3 bitangentWS,
                               vec2 uv, float viewSign, int materialIndex)
{
    SurfaceData surfaceData;
    surfaceData.lightLayerMask = 65535.0;

    const vec4 materialMetallicRoughness = hbgMaterialMetallicRoughness(materialIndex);
    const vec4 materialEmissiveFactor = hbgMaterialEmissiveFactor(materialIndex);
    const int bindlessFlags = int(hbgMaterialBindlessFlags(materialIndex).x + 0.5);

    vec4 baseColor = hbgSampleMaterialBaseColor(uv, materialIndex) * hbgMaterialBaseColorFactor(materialIndex);
    surfaceData.baseColor = baseColor.rgb;
    surfaceData.alpha = baseColor.a;

    float metallic = materialMetallicRoughness.x;
    float roughness = materialMetallicRoughness.y;

    surfaceData.ambientOcclusion = 1.0;
#ifdef HAS_MASK_MAP
    if ((bindlessFlags & 2) != 0)
    {
        vec4 mask = hbgSampleMaterialMetallicRoughness(uv, materialIndex);
        // glTF metallicRoughness: G = roughness, B = metallic.
        metallic *= mask.b;
        roughness *= mask.g;
        float occlusion = hbgSampleMaterialOcclusion(uv, materialIndex).r;
        float occlusionStrength = materialEmissiveFactor.w;
        surfaceData.ambientOcclusion = mix(1.0, occlusion, occlusionStrength);
    }
#endif
    surfaceData.materialAmbientOcclusion = surfaceData.ambientOcclusion;

    surfaceData.metallic = hbgSaturate(metallic);
    surfaceData.perceptualRoughness = hbgSaturate(roughness);
    surfaceData.roughness = max(surfaceData.perceptualRoughness * surfaceData.perceptualRoughness, 0.001);

    vec3 normalWS = geometricNormalWS;
#ifdef HAS_NORMAL_MAP
    if ((bindlessFlags & 1) != 0)
    {
        vec3 normalTS = hbgUnpackNormal(hbgSampleMaterialNormal(uv, materialIndex).xy,
                                        materialMetallicRoughness.z);
        vec3 tangent = tangentWS;
        vec3 bitangent = bitangentWS;
        if (dot(tangent, tangent) < 1.0e-8 || dot(bitangent, bitangent) < 1.0e-8)
        {
            // Derivative fallback for meshes without a valid tangent stream. The
            // asset layer normally generates MikkTSpace tangents, but imported
            // meshes without UVs can still reach this path.
            vec3 dp1 = dFdx(worldPos);
            vec3 dp2 = dFdy(worldPos);
            vec2 duv1 = dFdx(uv);
            vec2 duv2 = dFdy(uv);
            float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
            if (abs(determinant) > 1.0e-8)
            {
                tangent = normalize((dp1 * duv2.y - dp2 * duv1.y) / determinant);
                bitangent = cross(geometricNormalWS, tangent);
            }
        }
        mat3 tbn = mtxFromCols(tangent, bitangent, geometricNormalWS);
        normalWS = normalize(mul(normalTS, tbn));
    }
    else
    {
        normalWS = normalize(geometricNormalWS);
    }
#else
    normalWS = normalize(geometricNormalWS);
#endif

#ifdef DOUBLE_SIDED
    if (viewSign < 0.0)
    {
        normalWS = -normalWS;
    }
#endif

    surfaceData.normalWS = normalWS;
    surfaceData.geometricNormalWS = normalize(geometricNormalWS);

    vec3 emissive = hbgSampleMaterialEmissive(uv, materialIndex) * materialEmissiveFactor.rgb;
    surfaceData.emissive = max(emissive, vec3(0.0, 0.0, 0.0));
    surfaceData.contactShadowMask = 0.0;
    surfaceData.contactShadowFade = 0.0;

    return surfaceData;
}

#endif // HBG_SURFACE_DATA_BINDLESS_SC
