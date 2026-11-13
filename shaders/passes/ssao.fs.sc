$input v_texcoord0

#include "common/common.sc"
#include "material/normal_buffer.sc"

SAMPLER2D(s_depthPyramid, 4);
SAMPLER2D(s_gbuffer2, 2);

#define HBG_SSAO_DIRECTION_COUNT 8

float hbgSsaoInterleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

vec3 hbgSsaoViewPosition(vec2 uv, float rawDepth)
{
    const vec4 clip = vec4(uv * 2.0 - 1.0, rawDepth, 1.0);
    vec4 world = mul(u_global_invViewProj, clip);
    world.xyz /= max(world.w, 1.0e-6);
    return mul(u_global_view, vec4(world.xyz, 1.0)).xyz;
}

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const bool halfResolution = u_ssao_params2.z > 0.5;
    const vec2 aoSize = max(vec2(1.0, 1.0), ceil(screenSize * (halfResolution ? 0.5 : 1.0)));
    const vec2 texel = 1.0 / aoSize;
    const vec2 uv = clamp(v_texcoord0, texel * 0.5, vec2(1.0, 1.0) - texel * 0.5);
    const float rawDepth = texture2D(s_depthPyramid, uv).r;
    if (rawDepth >= 0.999999)
    {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const vec3 positionVS = hbgSsaoViewPosition(uv, rawDepth);
    const float linearDepth = max(-positionVS.z, 0.0001);
    const vec4 normalData = texture2D(s_gbuffer2, vec2(uv.x, 1.0 - uv.y));
    vec3 normalVS = normalize(mul(u_global_view, vec4(hbgDecodeNormalBuffer(normalData), 0.0)).xyz);
    if (dot(normalVS, normalVS) < 1.0e-4)
    {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const float radius = max(u_ssao_params.x, 0.001);
    const float radiusSq = radius * radius;
    const int stepCount = clamp(int(u_ssao_params2.y + 0.5), 2, 16);
    const int stepsPerSide = max(stepCount >> 1, 1);
    const float noise = hbgSsaoInterleavedGradientNoise(gl_FragCoord.xy);
    const float angularStep = HBG_PI * 2.0 / float(HBG_SSAO_DIRECTION_COUNT);

    const float radiusPixels =
        clamp(radius * abs(u_global_proj[1][1]) * aoSize.y * 0.5 / linearDepth, 1.0, 32.0);
    const float stepPixels = max(radiusPixels / float(stepsPerSide), 1.0);
    const float stepUV = stepPixels / aoSize.y;

    float occlusion = 0.0;
    int sampleCount = 0;
    for (int directionIndex = 0; directionIndex < HBG_SSAO_DIRECTION_COUNT; ++directionIndex)
    {
        const float angle = (float(directionIndex) + noise) * angularStep;
        const vec2 direction = vec2(cos(angle), sin(angle));

        for (int sideIndex = 0; sideIndex < 2; ++sideIndex)
        {
            const float side = sideIndex == 0 ? 1.0 : -1.0;
            for (int stepIndex = 0; stepIndex < stepsPerSide; ++stepIndex)
            {
                const float distancePixels = (float(stepIndex) + 1.0) * stepPixels;
                const vec2 sampleUV =
                    clamp(uv + direction * side * vec2(distancePixels / aoSize.x, stepUV),
                          texel * 0.5, vec2(1.0, 1.0) - texel * 0.5);
                const float sampleRawDepth = texture2DLod(s_depthPyramid, sampleUV, 0.0).r;
                if (sampleRawDepth >= 0.999999)
                {
                    continue;
                }

                const vec3 samplePositionVS = hbgSsaoViewPosition(sampleUV, sampleRawDepth);
                const vec3 delta = samplePositionVS - positionVS;
                const float distanceSq = dot(delta, delta);
                if (distanceSq < u_ssao_params.z * u_ssao_params.z)
                {
                    continue;
                }

                const float sampleLinearDepth = -samplePositionVS.z;
                if (sampleLinearDepth > linearDepth + u_ssao_params.w)
                {
                    continue;
                }
                sampleCount += 1;

                const vec3 sampleDirection = delta * inversesqrt(max(distanceSq, 1.0e-8));
                const float normalDot = dot(normalVS, sampleDirection);
                if (normalDot > u_ssao_params.z)
                {
                    const float distanceFalloff = hbgSaturate(1.0 - distanceSq / radiusSq);
                    occlusion += normalDot * mix(1.0, distanceFalloff, u_ssao_params2.x);
                }
            }
        }
    }

    const float normalizedOcclusion = occlusion / max(float(sampleCount), 1.0);
    const float visibility = hbgSaturate(1.0 - normalizedOcclusion * max(u_ssao_params.y, 0.0) * 2.0);
    gl_FragColor = vec4(visibility, visibility, visibility, 1.0);
}
