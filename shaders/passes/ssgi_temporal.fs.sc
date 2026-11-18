$input v_texcoord0

#include "common/common.sc"
#include "material/normal_buffer.sc"

SAMPLER2D(s_ssgiRaw, 0);
SAMPLER2D(s_motionVectors, 1);
SAMPLER2D(s_gbuffer2, 2);
SAMPLER2D(s_ssgiHistory, 3);
SAMPLER2D(s_depthPyramid, 4);
SAMPLER2D(s_ssgiHistoryDepth, 5);
SAMPLER2D(s_ssgiHistoryNormal, 6);

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const bool halfResolution = u_ssgi_params2.z > 0.5;
    const vec2 traceSize = max(vec2(1.0, 1.0), ceil(screenSize * (halfResolution ? 0.5 : 1.0)));
    const vec2 texel = 1.0 / traceSize;
    const vec2 uv = clamp(vec2(v_texcoord0.x, 1.0 - v_texcoord0.y), texel * 0.5,
                          vec2(1.0, 1.0) - texel * 0.5);
    const float rawDepth = texture2DLod(s_depthPyramid, uv, 0.0).r;

    const vec4 defaultNormal = hbgEncodeNormalBuffer(vec3(0.0, 0.0, 1.0), 1.0);
    gl_FragData[0] = vec4(0.0, 0.0, 0.0, 0.0);
    gl_FragData[1] = vec4(rawDepth, 0.0, 0.0, 0.0);
    gl_FragData[2] = defaultNormal;
    if (rawDepth >= 0.999999)
    {
        return;
    }

    const vec3 normalRaw =
        hbgDecodeNormalBuffer(texture2DLod(s_gbuffer2, uv, 0.0));
    const float normalLengthSq = dot(normalRaw, normalRaw);
    const bool validNormal = normalLengthSq > 1.0e-6;
    const vec3 normalWS = validNormal ? normalRaw * inversesqrt(normalLengthSq) : vec3(0.0, 0.0, 1.0);
    gl_FragData[2] = hbgEncodeNormalBuffer(normalWS, 1.0);

    const vec3 current =
        max(texture2DLod(s_ssgiRaw, uv, 0.0).rgb, vec3(0.0, 0.0, 0.0));
    const bool historyValid = u_ssgi_params4.x > 0.5;
    const float depthThreshold = max(u_ssgi_params4.y, 0.0001);
    const float maxFrames = max(u_ssgi_params4.z, 1.0);

    vec3 accumulated = current;
    float sampleCount = 0.0;
    if (historyValid && validNormal)
    {
        const vec2 motion = texture2DLod(s_motionVectors, uv, 0.0).xy;
        const vec2 previousUv = uv - vec2(motion.x, -motion.y);
        if (all(greaterThanEqual(previousUv, vec2(0.0, 0.0))) &&
            all(lessThanEqual(previousUv, vec2(1.0, 1.0))))
        {
            const float historyDepth = texture2DLod(s_ssgiHistoryDepth, previousUv, 0.0).r;
            const vec3 historyNormal =
                hbgDecodeNormalBuffer(texture2DLod(s_ssgiHistoryNormal, previousUv, 0.0));
            const bool normalMatches = dot(normalWS, historyNormal) >= 0.65;
            if (historyDepth < 0.999999 && abs(historyDepth - rawDepth) <= depthThreshold &&
                normalMatches)
            {
                const vec4 history = texture2DLod(s_ssgiHistory, previousUv, 0.0);
                sampleCount = clamp(history.a, 0.0, maxFrames);
                if (sampleCount > 0.5)
                {
                    const float accumulation =
                        sampleCount >= maxFrames ? 0.93 : sampleCount / (sampleCount + 1.0);
                    accumulated = mix(current, max(history.rgb, vec3(0.0, 0.0, 0.0)), accumulation);
                }
            }
        }
    }

    const float nextSampleCount = sampleCount < 0.5 ? 1.0 : min(sampleCount + 1.0, maxFrames);
    gl_FragData[0] = vec4(min(accumulated, vec3(8.0, 8.0, 8.0)), nextSampleCount);
    gl_FragData[1] = vec4(rawDepth, 0.0, 0.0, 0.0);
}
