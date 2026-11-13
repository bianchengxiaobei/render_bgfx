#ifndef HBG_DECAL_SC
#define HBG_DECAL_SC

#include "common/common.sc"
#include "material/surface_data_types.sc"

SAMPLER2D(s_decal0, 11);
SAMPLER2D(s_decal1, 12);
SAMPLER2D(s_decal2, 13);

SurfaceData hbgApplyDecals(SurfaceData surfaceData, vec2 screenUV)
{
    const vec4 decal0 = texture2D(s_decal0, screenUV);
    const vec4 decal1 = texture2D(s_decal1, screenUV);
    const vec4 decal2 = texture2D(s_decal2, screenUV);

    surfaceData.baseColor = surfaceData.baseColor * decal0.a + decal0.rgb;
    surfaceData.metallic = hbgSaturate(surfaceData.metallic * decal2.a + decal2.r);
    surfaceData.ambientOcclusion = hbgSaturate(surfaceData.ambientOcclusion * decal2.a + decal2.g);
    surfaceData.materialAmbientOcclusion = surfaceData.ambientOcclusion;
    surfaceData.perceptualRoughness =
        hbgSaturate(surfaceData.perceptualRoughness * decal2.a + decal2.b);
    surfaceData.roughness = max(surfaceData.perceptualRoughness * surfaceData.perceptualRoughness, 0.001);

    if (decal1.a < 0.999999)
    {
        const vec3 decalNormalWS = decal1.xyz * 2.0 - vec3(254.0 / 255.0, 254.0 / 255.0, 254.0 / 255.0);
        surfaceData.normalWS = normalize(surfaceData.normalWS * decal1.a + decalNormalWS);
    }

    return surfaceData;
}

#endif // HBG_DECAL_SC
