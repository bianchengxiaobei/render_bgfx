$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_ssaoInput, 0);
SAMPLER2D(s_motionVectors, 1);
SAMPLER2D(s_ssaoHistory, 2);
SAMPLER2D(s_depthPyramid, 4);

vec2 hbgSsaoPackDepth(float depth)
{
    const float scaled = floor(clamp(depth, 0.0, 1.0) * 65535.0 + 0.5);
    const float high = floor(scaled / 256.0);
    const float low = scaled - high * 256.0;
    return vec2(low, high) / 255.0;
}

float hbgSsaoUnpackDepth(vec2 packed)
{
    const float low = floor(clamp(packed.x, 0.0, 1.0) * 255.0 + 0.5);
    const float high = floor(clamp(packed.y, 0.0, 1.0) * 255.0 + 0.5);
    return (low + high * 256.0) / 65535.0;
}

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const bool halfResolution = u_ssao_params2.z > 0.5;
    const vec2 aoSize = max(vec2(1.0, 1.0), ceil(screenSize * (halfResolution ? 0.5 : 1.0)));
    const vec2 texel = 1.0 / aoSize;
    const vec2 uv = clamp(v_texcoord0, texel * 0.5, vec2(1.0, 1.0) - texel * 0.5);

    const float currentAO = texture2D(s_ssaoInput, uv).r;
    const float currentDepth = texture2D(s_depthPyramid, uv).r;

    const vec2 motion = texture2D(s_motionVectors, uv).xy;
    const vec2 previousUvUnclamped = uv - vec2(motion.x, -motion.y);
    const bool offScreen = any(previousUvUnclamped < vec2(0.0, 0.0)) ||
                           any(previousUvUnclamped > vec2(1.0, 1.0));
    const vec2 previousUv = clamp(previousUvUnclamped, texel * 0.5, vec2(1.0, 1.0) - texel * 0.5);

    const vec4 history = texture2D(s_ssaoHistory, previousUv);
    const float previousAO = history.r;
    const float previousDepth = hbgSsaoUnpackDepth(history.gb);

    float minimum = currentAO;
    float maximum = currentAO;
    float average = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const vec2 sampleUv = clamp(uv + vec2(float(x), float(y)) * texel,
                                        texel * 0.5, vec2(1.0, 1.0) - texel * 0.5);
            const float sampleAO = texture2D(s_ssaoInput, sampleUv).r;
            minimum = min(minimum, sampleAO);
            maximum = max(maximum, sampleAO);
            average += sampleAO;
        }
    }
    average /= 9.0;

    const float nudge = 0.10 * abs(average - currentAO);
    minimum -= nudge;
    maximum += nudge;

    const float depthTolerance = max(currentDepth * 0.02, 0.0005);
    const float depthWeight =
        1.0 - hbgSaturate(abs(previousDepth - currentDepth) / depthTolerance);
    const float motionPixels = length(motion * aoSize);
    const float motionWeight =
        1.0 - hbgSaturate(motionPixels / max(u_ssao_params3.z, 1.0));

    float historyWeight = u_ssao_params3.y * depthWeight * motionWeight;
    if (u_ssao_params3.x < 0.5 || offScreen)
    {
        historyWeight = 0.0;
    }
    historyWeight = hbgSaturate(historyWeight);

    const float historyAO = clamp(previousAO, minimum, maximum);
    const float resultAO = mix(currentAO, historyAO, historyWeight);
    gl_FragColor = vec4(resultAO, hbgSsaoPackDepth(currentDepth), 1.0);
}
