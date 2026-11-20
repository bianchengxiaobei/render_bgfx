$input v_texcoord0

#define HBG_ADVANCED_MATERIAL 1

#include "common/common.sc"
#include "lighting/gbuffer_decode.sc"
#include "material/material_gbuffer.sc"
#include "lighting/direct_lighting.sc"

SAMPLER2D(s_depthPyramid, 4);
SAMPLER2D(s_ssao, 11);
SAMPLER2D(s_gbuffer4, 12);
SAMPLER2D(s_bakedIndirect, 13);

vec3 hbgEvaluateAmbientLightmapOffset(SurfaceData surfaceData, vec3 viewWS, vec3 ambientColor,
                                      float ambientIntensity)
{
    const vec3 N = normalize(surfaceData.normalWS);
    const vec3 V = normalize(viewWS);
    const float NdotV = max(dot(N, V), 1.0e-4);
    const vec3 f0 = hbgAdvancedFresnel0(surfaceData, NdotV);
    const vec3 fresnel = hbgF_Schlick(f0, NdotV);
    const vec3 skyColor = mix(vec3(0.25, 0.30, 0.40), vec3(0.75, 0.82, 1.0),
                              hbgSaturate(N.y * 0.5 + 0.5));
    vec3 result = (surfaceData.baseColor * (1.0 - surfaceData.metallic) *
                       surfaceData.ambientOcclusion +
                   fresnel * skyColor * (1.0 - surfaceData.roughness * 0.5) *
                       surfaceData.ambientOcclusion) *
                  ambientColor * ambientIntensity;

    if (u_environment_skyParams.z > 0.5)
    {
        const float intensity = u_environment_skyParams.x;
        result += surfaceData.baseColor * (1.0 - surfaceData.metallic) *
                  surfaceData.ambientOcclusion * hbgSampleSH9(N) * intensity;
#if (HBG_VARIANT_MATERIAL_FLAGS & 4)
        result += surfaceData.baseColor * hbgSaturate(surfaceData.subsurfaceMask) *
                  hbgSampleSH9(N) * intensity * 0.25;
#endif
    }
    return result;
}

void main()
{
    const vec2 gbufferUV = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    const vec4 baked = texture2D(s_bakedIndirect, gbufferUV);
    if (baked.a < 0.5)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    SurfaceData surfaceData = hbgDecodeGBufferSurface(gbufferUV);
    surfaceData = hbgDecodeMaterialAdvanced(texture2D(s_gbuffer4, gbufferUV), surfaceData);

    const float depth = texture2D(s_depthPyramid, v_texcoord0).r;
    if (depth >= 0.999999)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    if (u_ssao_params2.w > 0.5)
    {
        surfaceData.ambientOcclusion *= hbgSaturate(texture2D(s_ssao, v_texcoord0).r);
    }

    const vec2 ndc = v_texcoord0 * vec2(2.0, 2.0) - vec2(1.0, 1.0);
    vec4 worldPosition = mul(u_global_invViewProj, vec4(ndc, depth, 1.0));
    worldPosition.xyz /= worldPosition.w;
    const vec3 viewWS = u_global_cameraPos.xyz - worldPosition.xyz;

    const vec3 ambient =
        hbgEvaluateAmbientLightmapOffset(surfaceData, viewWS, u_pass_ambientColor.rgb,
                                         u_pass_ambientColor.w);
    gl_FragColor = vec4(baked.rgb - ambient, 0.0);
}
