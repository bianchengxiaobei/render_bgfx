$input v_texcoord0

#include "common/common.sc"
#include "lighting/environment.sc"
#include "material/normal_buffer.sc"

SAMPLER2D(s_gbuffer0, 0);
SAMPLER2D(s_gbuffer1, 1);
SAMPLER2D(s_gbuffer2, 2);
SAMPLER2D(s_depthPyramid, 4);
SAMPLER2D(s_ssao, 11);
SAMPLER2D(s_hdrColor, 12);

vec3 hbgSsrWorldPosition(vec2 uv, float rawDepth)
{
    vec4 world = mul(u_global_invViewProj, vec4(uv * 2.0 - 1.0, rawDepth, 1.0));
    world.xyz /= max(world.w, 1.0e-6);
    return world.xyz;
}

float hbgSsrLinearDepth(vec3 worldPosition)
{
    return max(-mul(u_global_view, vec4(worldPosition, 1.0)).z, 0.0);
}

vec3 hbgSsrSpecularWeight(vec3 baseColor, float metallic, float perceptualRoughness, float NdotV)
{
    const vec3 f0 = mix(vec3(0.04, 0.04, 0.04), baseColor, metallic);
    const vec2 ab = hbgEnvBRDFApprox(NdotV, perceptualRoughness);
    return max(f0 * ab.x + ab.y, vec3(0.0, 0.0, 0.0));
}

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const vec2 texel = 1.0 / screenSize;
    const vec2 uv = clamp(v_texcoord0, texel * 0.5, vec2(1.0, 1.0) - texel * 0.5);
    const float rawDepth = texture2D(s_depthPyramid, uv).r;
    if (rawDepth >= 0.999999)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const vec2 gbufferUV = vec2(uv.x, 1.0 - uv.y);
    const vec4 gbuffer0 = texture2D(s_gbuffer0, gbufferUV);
    const vec4 gbuffer1 = texture2D(s_gbuffer1, gbufferUV);
    const vec4 gbuffer2 = texture2D(s_gbuffer2, gbufferUV);
    const vec3 baseColor = gbuffer0.rgb;
    const float metallic = hbgSaturate(gbuffer1.r);
    const float perceptualRoughness = hbgSaturate(gbuffer2.a);
    const vec3 normalWS = normalize(hbgDecodeNormalBuffer(gbuffer2));
    if (dot(normalWS, normalWS) < 1.0e-4)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const vec3 worldPosition = hbgSsrWorldPosition(uv, rawDepth);
    const vec3 viewWS = normalize(u_global_cameraPos.xyz - worldPosition);
    const float NdotV = max(dot(normalWS, viewWS), 1.0e-4);
    const vec3 reflectionWS = reflect(-viewWS, normalWS);
    const vec3 specularWeight = hbgSsrSpecularWeight(baseColor, metallic, perceptualRoughness, NdotV);

    float ambientOcclusion = hbgSaturate(gbuffer1.g);
    if (u_ssao_params2.w > 0.5)
    {
        ambientOcclusion *= hbgSaturate(texture2D(s_ssao, uv).r);
    }

    const float roughnessWeight =
        1.0 - smoothstep(0.0, max(u_ssr_params.w, 1.0e-4), perceptualRoughness);
    const float intensity = max(u_ssr_params2.y, 0.0);

    if (dot(normalWS, viewWS) <= 0.0 || dot(reflectionWS, normalWS) <= 0.0 || roughnessWeight <= 0.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const float maxDistance = max(u_ssr_params.x, 0.1);
    const int stepCount = clamp(int(u_ssr_params.y + 0.5), 1, 64);
    const float thickness = max(u_ssr_params.z, 0.001);
    const float bias = max(0.01, maxDistance * 0.001);
    const vec3 rayOrigin = worldPosition + normalWS * bias;

    bool found = false;
    vec2 hitUV = uv;
    float previousDelta = 0.0;
    bool hasPreviousDelta = false;
    for (int stepIndex = 1; stepIndex <= stepCount; ++stepIndex)
    {
        const float distance = maxDistance * float(stepIndex) / float(stepCount);
        const vec3 rayPosition = rayOrigin + reflectionWS * distance;
        const vec4 clip = mul(u_global_viewProj, vec4(rayPosition, 1.0));
        if (clip.w <= 1.0e-5)
        {
            break;
        }

        const vec3 ndc = clip.xyz / clip.w;
        if (ndc.x <= -1.0 || ndc.x >= 1.0 || ndc.y <= -1.0 || ndc.y >= 1.0)
        {
            break;
        }
        if (ndc.z < -1.0001 || ndc.z > 1.0001)
        {
            break;
        }

        const vec2 sampleUV = ndc.xy * 0.5 + 0.5;
        const float sampleRawDepth = texture2DLod(s_depthPyramid, sampleUV, 0.0).r;
        if (sampleRawDepth >= 0.999999)
        {
            break;
        }

        const float rayDepth = hbgSsrLinearDepth(rayPosition);
        const float sceneDepth = hbgSsrLinearDepth(hbgSsrWorldPosition(sampleUV, sampleRawDepth));
        const float delta = rayDepth - sceneDepth;
        const bool crossedSurface = hasPreviousDelta && previousDelta > 0.0 && delta <= 0.0;
        if (crossedSurface || (delta > bias && delta < thickness + bias))
        {
            hitUV = sampleUV;
            found = true;
            break;
        }
        previousDelta = delta;
        hasPreviousDelta = true;
    }

    vec3 reflection = vec3(0.0, 0.0, 0.0);
    if (found)
    {
        const float edgeDistance = min(min(hitUV.x, 1.0 - hitUV.x), min(hitUV.y, 1.0 - hitUV.y));
        const float edgeWeight = smoothstep(0.0, max(u_ssr_params2.x, 1.0e-4), edgeDistance);
        const vec3 traced = texture2DLod(s_hdrColor, hitUV, 0.0).rgb *
                            (specularWeight * ambientOcclusion * intensity);
        reflection = max(traced, vec3(0.0, 0.0, 0.0)) * (edgeWeight * roughnessWeight);
    }

    gl_FragColor = vec4(reflection, found ? 1.0 : 0.0);
}
