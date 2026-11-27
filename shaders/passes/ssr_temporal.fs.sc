$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_ssrInput, 0);
SAMPLER2D(s_motionVectors, 1);
SAMPLER2D(s_ssrHistory, 2);
SAMPLER2D(s_depthPyramid, 4);

void main()
{
    const vec2 screenSize = max(u_global_screenSize.xy, vec2(1.0, 1.0));
    const vec2 texel = 1.0 / screenSize;
    const vec2 uv = clamp(vec2(v_texcoord0.x, 1.0 - v_texcoord0.y), texel * 0.5,
                          vec2(1.0, 1.0) - texel * 0.5);

    const vec4 currentSample = texture2D(s_ssrInput, uv);
    const vec3 currentReflection = max(currentSample.rgb, vec3(0.0, 0.0, 0.0));
    const bool currentFound = currentSample.a > 0.5;
    const float currentDepth = texture2D(s_depthPyramid, uv).r;

    const vec2 motion = texture2D(s_motionVectors, uv).xy;
    const vec2 previousUvUnclamped = uv - vec2(motion.x, -motion.y);
    const bool offScreen = any(previousUvUnclamped < vec2(0.0, 0.0)) ||
                           any(previousUvUnclamped > vec2(1.0, 1.0));
    const vec2 previousUv = clamp(previousUvUnclamped, texel * 0.5, vec2(1.0, 1.0) - texel * 0.5);

    const vec4 history = texture2D(s_ssrHistory, previousUv);
    const vec3 historyReflection = max(history.rgb, vec3(0.0, 0.0, 0.0));
    const float historyDepth = history.a;

    const float depthTolerance = max(currentDepth * 0.02, 0.0005);
    const float depthWeight = 1.0 - hbgSaturate(abs(historyDepth - currentDepth) / depthTolerance);
    const float motionPixels = length(motion * screenSize);
    const float motionWeight = 1.0 - hbgSaturate(motionPixels / max(u_ssr_params3.z, 1.0));

    float historyWeight = currentFound ? u_ssr_params3.y : 0.0;
    if (u_ssr_params3.x < 0.5 || offScreen)
    {
        historyWeight = 0.0;
    }
    historyWeight = hbgSaturate(historyWeight * depthWeight * motionWeight);

    const vec3 result = mix(currentReflection, historyReflection, historyWeight);
    gl_FragColor = vec4(result, currentDepth);
}
