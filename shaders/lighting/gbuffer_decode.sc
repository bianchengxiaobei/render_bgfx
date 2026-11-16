#ifndef HBG_GBUFFER_DECODE_SC
#define HBG_GBUFFER_DECODE_SC

#include "common/common.sc"
#include "material/normal_buffer.sc"
#include "material/surface_data_types.sc"

SAMPLER2D(s_gbuffer0, 0);
SAMPLER2D(s_gbuffer1, 1);
SAMPLER2D(s_gbuffer2, 2);
SAMPLER2D(s_gbuffer3, 3);

SurfaceData hbgDecodeGBufferSurface(vec2 uv)
{
    const vec4 gbuffer0 = texture2D(s_gbuffer0, uv);
    const vec4 gbuffer1 = texture2D(s_gbuffer1, uv);
    const vec4 gbuffer2 = texture2D(s_gbuffer2, uv);
    const vec4 gbuffer3 = texture2D(s_gbuffer3, uv);

    SurfaceData surfaceData;
    surfaceData.baseColor = gbuffer0.rgb;
    surfaceData.alpha = gbuffer0.a;
    surfaceData.metallic = hbgSaturate(gbuffer1.r);
    surfaceData.ambientOcclusion = hbgSaturate(gbuffer1.g);
    surfaceData.materialAmbientOcclusion = surfaceData.ambientOcclusion;
    surfaceData.perceptualRoughness = hbgSaturate(gbuffer2.a);
    surfaceData.roughness = max(surfaceData.perceptualRoughness * surfaceData.perceptualRoughness, 0.001);
    surfaceData.normalWS = hbgDecodeNormalBuffer(gbuffer2);
    surfaceData.geometricNormalWS = surfaceData.normalWS;
    surfaceData.emissive = max(gbuffer3.rgb, vec3(0.0, 0.0, 0.0));
    surfaceData.contactShadowMask = floor(hbgSaturate(gbuffer1.b) * 255.0 + 0.5);
    surfaceData.contactShadowFade = hbgSaturate(gbuffer1.a);
    surfaceData.lightLayerMask = 65535.0;
    if (u_object_lightLayerMask.w > 0.5)
    {
        surfaceData.lightLayerMask = floor(hbgSaturate(gbuffer0.a) * 255.0 + 0.5) +
                                     floor(hbgSaturate(gbuffer3.a) * 255.0 + 0.5) * 256.0;
    }
    return surfaceData;
}

#endif // HBG_GBUFFER_DECODE_SC
