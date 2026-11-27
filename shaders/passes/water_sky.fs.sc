$input v_texcoord0

#include "common/common.sc"
#include "lighting/gbuffer_decode.sc"
#define HBG_OCEAN_SLOPE_VARIANCE_TEXTURE 1
#include "lighting/ocean_brdf.sc"
#include "lighting/shadows.sc"

SAMPLER2D(s_depthSource, 5);

void main()
{
    const vec2 gbufferUV = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    const SurfaceData surfaceData = hbgDecodeGBufferSurface(gbufferUV);
    if (surfaceData.waterMask <= 0.5)
    {
        discard;
    }

    const float depth = texture2D(s_depthSource, v_texcoord0).r;
    const vec2 ndc = v_texcoord0 * vec2(2.0, 2.0) - vec2(1.0, 1.0);
    vec4 worldPosition = mul(u_global_invViewProj, vec4(ndc, depth, 1.0));
    worldPosition.xyz /= worldPosition.w;

    const vec3 N = normalize(surfaceData.normalWS);
    const vec3 V = normalize(u_global_cameraPos.xyz - worldPosition.xyz);
    const vec3 L = normalize(u_pass_lightDirection.xyz);
    const float shadowVisibility = hbgSampleDirectionalShadow(worldPosition.xyz, N);
    const vec3 sunRadiance = u_pass_lightColor.rgb * max(u_pass_lightDirection.w, 0.0) *
                             shadowVisibility;
    const vec3 skyIrradiance =
        hbgOceanSkyIrradiance(worldPosition.xyz, V, N, surfaceData.perceptualRoughness);
    const vec2 oceanCoord = worldPosition.xz;
    const float seaRoughness =
        max(hbgOceanSlopeVariance(dFdx(oceanCoord), dFdy(oceanCoord)), 2.0e-5);
    const vec3 ocean = hbgEvaluateOceanBrdf(V, N, L, sunRadiance, skyIrradiance,
                                            surfaceData.baseColor, seaRoughness);
    gl_FragColor = vec4(max(ocean, vec3(0.0, 0.0, 0.0)), 1.0);
}
