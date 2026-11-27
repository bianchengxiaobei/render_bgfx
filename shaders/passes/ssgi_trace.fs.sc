$input v_texcoord0

#include "common/common.sc"
#include "lighting/environment.sc"
#include "material/normal_buffer.sc"

SAMPLER2D(s_gbuffer2, 2);
SAMPLER2D(s_depthPyramid, 4);
SAMPLER2D(s_hdrColor, 12);

float hbgSsgiNoise(vec2 pixel)
{
    vec2 p = fract(pixel * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

vec3 hbgSsgiCosineSample(vec2 sample, vec3 normalWS)
{
    const float radius = sqrt(max(sample.x, 0.0));
    const float phi = 2.0 * HBG_PI * sample.y;
    const float z = sqrt(max(1.0 - sample.x, 0.0));
    const vec3 up = abs(normalWS.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    const vec3 tangent = normalize(cross(up, normalWS));
    const vec3 bitangent = cross(normalWS, tangent);
    return normalize(tangent * (radius * cos(phi)) + bitangent * (radius * sin(phi)) + normalWS * z);
}

vec3 hbgSsgiWorldPosition(vec2 uv, float rawDepth)
{
    vec4 world = mul(u_global_invViewProj, vec4(uv * 2.0 - 1.0, rawDepth, 1.0));
    world.xyz /= max(world.w, 1.0e-6);
    return world.xyz;
}

float hbgSsgiLinearDepth(vec3 worldPosition)
{
    return max(-mul(u_global_view, vec4(worldPosition, 1.0)).z, 0.0);
}

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const bool halfResolution = u_ssgi_params2.z > 0.5;
    const vec2 traceSize = max(vec2(1.0, 1.0), ceil(screenSize * (halfResolution ? 0.5 : 1.0)));
    const vec2 texel = 1.0 / traceSize;
    const vec2 uv = clamp(v_texcoord0, texel * 0.5, vec2(1.0, 1.0) - texel * 0.5);
    const float rawDepth = texture2D(s_depthPyramid, uv).r;
    if (rawDepth >= 0.999999)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    const vec3 normalRaw =
        hbgDecodeNormalBuffer(texture2D(s_gbuffer2, vec2(uv.x, 1.0 - uv.y)));
    const float normalLengthSq = dot(normalRaw, normalRaw);
    if (normalLengthSq < 1.0e-6)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const vec3 normalWS = normalRaw * inversesqrt(normalLengthSq);

    const vec3 worldPosition = hbgSsgiWorldPosition(uv, rawDepth);
    const float maxDistance = max(u_ssgi_params.y, 0.1);
    const int stepCount = clamp(int(u_ssgi_params.z + 0.5), 1, 16);
    const int rayCount = clamp(int(u_ssgi_params.w + 0.5), 1, 32);
    const float falloff = max(u_ssgi_params2.x, 0.0);
    const float thickness = max(u_ssgi_params2.y, 0.001);
    const float bias = max(0.02, maxDistance * 0.001);
    const float stepDistance = maxDistance / float(stepCount);
    const vec3 rayOrigin = worldPosition + normalWS * bias;
    vec3 ambient = hbgSampleSH9(normalWS);
    if (u_environment_skyParams.z < 0.5)
    {
        ambient = u_pass_ambientColor.rgb * u_pass_ambientColor.w;
    }
    const float noise = hbgSsgiNoise(gl_FragCoord.xy);

    vec3 radianceSum = vec3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    for (int rayIndex = 0; rayIndex < 32; ++rayIndex)
    {
        if (rayIndex >= rayCount)
        {
            break;
        }

        const float u1 = fract(noise + float(rayIndex) * 0.61803398875);
        const float u2 = fract(noise * 1.3247179572 + float(rayIndex) * 0.7548776662);
        const float jitter = fract(noise + float(rayIndex) * 0.41421356237);
        const vec3 direction = hbgSsgiCosineSample(vec2(u1, u2), normalWS);

        vec3 radiance = ambient;
        float rayWeight = 1.0;
        float previousDelta = 0.0;
        bool hasPreviousDelta = false;
        for (int stepIndex = 1; stepIndex <= 16; ++stepIndex)
        {
            if (stepIndex > stepCount)
            {
                break;
            }

            const float distance = (float(stepIndex) - 0.5 + jitter) * stepDistance;
            const vec3 rayPosition = rayOrigin + direction * distance;
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

            const float rayDepth = hbgSsgiLinearDepth(rayPosition);
            const float sceneDepth = hbgSsgiLinearDepth(hbgSsgiWorldPosition(sampleUV, sampleRawDepth));
            const float delta = rayDepth - sceneDepth;
            const bool crossedSurface = hasPreviousDelta && previousDelta > 0.0 && delta <= 0.0;
            if (crossedSurface || (delta > bias && delta < thickness + bias))
            {
                const float normalizedDistance = distance / maxDistance;
                radiance = min(max(texture2DLod(s_hdrColor, sampleUV, 0.0).rgb, vec3(0.0, 0.0, 0.0)),
                                vec3(8.0, 8.0, 8.0));
                rayWeight = 1.0 / (1.0 + falloff * normalizedDistance * normalizedDistance);
                break;
            }

            previousDelta = delta;
            hasPreviousDelta = true;
        }

        radianceSum += radiance * rayWeight;
        weightSum += rayWeight;
    }

    const vec3 averageRadiance = weightSum > 1.0e-4 ? radianceSum / weightSum : ambient;
    const vec3 indirect = max(averageRadiance - ambient * u_ssgi_params3.x, vec3(0.0, 0.0, 0.0));
    gl_FragColor = vec4(indirect, 0.0);
}
