#ifndef HBG_DECAL_SC
#define HBG_DECAL_SC

#include "common/common.sc"
#include "material/surface_data_types.sc"

SAMPLER2D(s_decal0, 11);
SAMPLER2D(s_decal1, 12);
SAMPLER2D(s_decal2, 13);
SAMPLER2D(s_decal3, 14);

SurfaceData hbgApplyDecals(SurfaceData surfaceData, vec2 screenUV)
{
    const vec4 decal0 = texture2D(s_decal0, screenUV);
    const vec4 decal1 = texture2D(s_decal1, screenUV);
    const vec4 decal2 = texture2D(s_decal2, screenUV);
    const vec4 decal3 = texture2D(s_decal3, screenUV);

    const int receiverLayerMask = int(floor(u_object_lightLayerMask.y + 0.5));
    const int decalLayerMask = int(floor(decal3.z * 255.0 + 0.5));
    SurfaceData result = surfaceData;
    const bool layerVisible = receiverLayerMask <= 0 || decalLayerMask <= 0 ||
                              (receiverLayerMask & decalLayerMask) != 0;
    const float layerWeight = layerVisible ? 1.0 : 0.0;
    result.baseColor = mix(surfaceData.baseColor, decal0.rgb, hbgSaturate(decal0.a) * layerWeight);
    result.metallic = mix(surfaceData.metallic, decal2.r, hbgSaturate(decal3.x) * layerWeight);
    result.ambientOcclusion = mix(surfaceData.ambientOcclusion, decal2.g, hbgSaturate(decal2.a) * layerWeight);
    result.materialAmbientOcclusion = result.ambientOcclusion;
    result.perceptualRoughness =
        mix(surfaceData.perceptualRoughness, decal2.b, hbgSaturate(decal3.y) * layerWeight);
    result.roughness = max(result.perceptualRoughness * result.perceptualRoughness, 0.001);

    if (decal1.a * layerWeight > 0.0001)
    {
        const vec3 decalNormalWS = decal1.xyz * 2.0 - vec3(254.0 / 255.0, 254.0 / 255.0, 254.0 / 255.0);
        result.normalWS = normalize(mix(surfaceData.normalWS, decalNormalWS, hbgSaturate(decal1.a) * layerWeight));
    }

    return result;
}
#endif // HBG_DECAL_SC
