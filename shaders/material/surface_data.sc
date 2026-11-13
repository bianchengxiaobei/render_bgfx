#ifndef HBG_SURFACE_DATA_SC
#define HBG_SURFACE_DATA_SC

#include "common/common.sc"
#include "common/packing.sc"
#include "common/brdf.sc"
#include "material/surface_data_types.sc"

SAMPLER2D(s_baseColorMap, 0);
SAMPLER2D(s_metallicRoughnessMap, 1);
SAMPLER2D(s_normalMap, 2);
SAMPLER2D(s_occlusionMap, 3);
SAMPLER2D(s_emissiveMap, 4);

SurfaceData hbgMakeSurfaceData(vec3 worldPos, vec3 geometricNormalWS, vec3 tangentWS, vec3 bitangentWS,
                               vec2 uv, float viewSign)
{
    SurfaceData surfaceData;

    vec4 baseColor = texture2D(s_baseColorMap, uv) * u_material_baseColorFactor;
    surfaceData.baseColor = baseColor.rgb;
    surfaceData.alpha = baseColor.a;

    float metallic = u_material_metallicRoughness.x;
    float roughness = u_material_metallicRoughness.y;

#ifdef HAS_MASK_MAP
    vec4 mask = texture2D(s_metallicRoughnessMap, uv);
    // glTF metallicRoughness: G = roughness, B = metallic.
    metallic *= mask.b;
    roughness *= mask.g;
    float occlusion = texture2D(s_occlusionMap, uv).r;
    float occlusionStrength = u_material_emissiveFactor.w;
    surfaceData.ambientOcclusion = mix(1.0, occlusion, occlusionStrength);
#else
    surfaceData.ambientOcclusion = 1.0;
#endif
    surfaceData.materialAmbientOcclusion = surfaceData.ambientOcclusion;

    surfaceData.metallic = hbgSaturate(metallic);
    surfaceData.perceptualRoughness = hbgSaturate(roughness);
    surfaceData.roughness = max(surfaceData.perceptualRoughness * surfaceData.perceptualRoughness, 0.001);

    vec3 normalWS = geometricNormalWS;
#ifdef HAS_NORMAL_MAP
    vec3 normalTS = hbgUnpackNormal(texture2D(s_normalMap, uv).xy,
                                    u_material_metallicRoughness.z);
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

    vec3 emissive = texture2D(s_emissiveMap, uv).rgb * u_material_emissiveFactor.rgb;
    surfaceData.emissive = max(emissive, vec3(0.0, 0.0, 0.0));
    surfaceData.contactShadowMask = 0.0;
    surfaceData.contactShadowFade = 0.0;

    return surfaceData;
}

#endif // HBG_SURFACE_DATA_SC
