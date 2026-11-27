#define HBG_ADVANCED_MATERIAL 1

$input v_texcoord0

#include "common/common.sc"
#include "lighting/gbuffer_decode.sc"
#include "material/material_gbuffer.sc"
#include "lighting/light_loop.sc"
#include "lighting/ocean_brdf.sc"

SAMPLER2D(s_depthPyramid, 4);
SAMPLER2D(s_ssao, 11);
SAMPLER2D(s_gbuffer4, 12);

void main()
{
    const vec2 gbufferUV = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    SurfaceData surfaceData = hbgDecodeGBufferSurface(gbufferUV);
    surfaceData = hbgDecodeMaterialAdvanced(texture2D(s_gbuffer4, gbufferUV), surfaceData);

    const float depth = texture2D(s_depthPyramid, v_texcoord0).r;
    const vec2 ndc = v_texcoord0 * vec2(2.0, 2.0) - vec2(1.0, 1.0);
    vec4 worldPosition = mul(u_global_invViewProj, vec4(ndc, depth, 1.0));
    worldPosition.xyz /= worldPosition.w;

    if (u_ssao_params2.w > 0.5)
    {
        surfaceData.ambientOcclusion *= hbgSaturate(texture2D(s_ssao, v_texcoord0).r);
    }

    const vec3 viewWS = u_global_cameraPos.xyz - worldPosition.xyz;
    const float shadowVisibility =
        hbgSampleDirectionalShadow(worldPosition.xyz, surfaceData.normalWS);
    if (surfaceData.waterMask > 0.5)
    {
        const vec3 L = normalize(u_pass_lightDirection.xyz);
        const vec3 sunRadiance = u_pass_lightColor.rgb * max(u_pass_lightDirection.w, 0.0) *
                                 shadowVisibility;
        const vec3 N = normalize(surfaceData.normalWS);
        const vec3 V = normalize(viewWS);
        const vec3 skyIrradiance =
            hbgOceanSkyIrradiance(worldPosition.xyz, V, N, surfaceData.perceptualRoughness);
        const vec3 ocean = hbgEvaluateOceanBrdf(V, N, L, sunRadiance, skyIrradiance,
                                                surfaceData.baseColor,
                                                max(surfaceData.roughness * 0.25, 2.0e-5));
        gl_FragColor = vec4(max(ocean, vec3(0.0, 0.0, 0.0)), 1.0);
        return;
    }
    const vec3 color = hbgEvaluateLightLoop(surfaceData, viewWS, worldPosition.xyz, shadowVisibility) +
                       surfaceData.emissive;

    gl_FragColor = vec4(max(color, vec3(0.0, 0.0, 0.0)), 1.0);
}