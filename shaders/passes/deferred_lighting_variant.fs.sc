#define HBG_VARIANT_MODE 1

#ifndef HBG_VARIANT_LIGHT_FLAGS
#define HBG_VARIANT_LIGHT_FLAGS 0xffffffffu
#endif
#ifndef HBG_VARIANT_MATERIAL_FLAGS
#define HBG_VARIANT_MATERIAL_FLAGS 1u
#endif

#if ((HBG_VARIANT_MATERIAL_FLAGS & 0x7eu) != 0)
#define HBG_ADVANCED_MATERIAL 1
#endif

$input v_texcoord0

#include "common/common.sc"
#include "lighting/gbuffer_decode.sc"
#if ((HBG_VARIANT_MATERIAL_FLAGS & 0x7eu) != 0)
#include "material/material_gbuffer.sc"
#endif
#include "lighting/light_loop.sc"

SAMPLER2D(s_depthPyramid, 4);
SAMPLER2D(s_ssao, 11);
#if ((HBG_VARIANT_MATERIAL_FLAGS & 0x7eu) != 0)
SAMPLER2D(s_gbuffer4, 12);
#endif

void main()
{
    const vec2 gbufferUV = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    SurfaceData surfaceData = hbgDecodeGBufferSurface(gbufferUV);
#if ((HBG_VARIANT_MATERIAL_FLAGS & 0x7eu) != 0)
    surfaceData = hbgDecodeMaterialAdvanced(texture2D(s_gbuffer4, gbufferUV), surfaceData);
#endif

    const float depth = texture2D(s_depthPyramid, v_texcoord0).r;
    const vec2 ndc = v_texcoord0 * vec2(2.0, 2.0) - vec2(1.0, 1.0);
    vec4 worldPosition = mul(u_global_invViewProj, vec4(ndc, depth, 1.0));
    worldPosition.xyz /= worldPosition.w;

    if (u_ssao_params2.w > 0.5)
    {
        surfaceData.ambientOcclusion *= hbgSaturate(texture2D(s_ssao, v_texcoord0).r);
    }

    const vec3 viewWS = u_global_cameraPos.xyz - worldPosition.xyz;
    float shadowVisibility = 1.0;
#if ((HBG_VARIANT_LIGHT_FLAGS & 8u) != 0)
    shadowVisibility = hbgSampleDirectionalShadow(worldPosition.xyz, surfaceData.normalWS);
#endif
    const vec3 color = hbgEvaluateLightLoop(surfaceData, viewWS, worldPosition.xyz, shadowVisibility) +
                       surfaceData.emissive;

    gl_FragColor = vec4(max(color, vec3(0.0, 0.0, 0.0)), 1.0);
}
