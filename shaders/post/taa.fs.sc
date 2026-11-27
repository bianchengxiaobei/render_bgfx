$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_source, 0);
SAMPLER2D(s_taaHistory, 1);
SAMPLER2D(s_taaMotionVectors, 2);
SAMPLER2D(s_taaDepth, 3);

void main()
{
    const vec2 uv = v_texcoord0;
    const vec2 texel = u_postprocess_screenParams.zw;

    // Fullscreen passes flip the sampled texture into their target. Keep the
    // TAA color, motion and depth lookups in the same source-texture space so
    // the resolved output preserves the post-process chain orientation.
    const vec2 sourceUv = vec2(uv.x, 1.0 - uv.y);
    const vec3 current = max(texture2D(s_source, sourceUv).rgb, vec3(0.0, 0.0, 0.0));

    const vec2 motion = texture2D(s_taaMotionVectors, sourceUv).xy;
    // sourceUv is vertically mirrored, so scene-space motion flips on Y.
    const vec2 prevUv = sourceUv - vec2(motion.x, -motion.y);
    const bool offScreen = any(prevUv < vec2(0.0, 0.0)) || any(prevUv > vec2(1.0, 1.0));
    const vec2 historyUv = clamp(prevUv, vec2(0.0, 0.0), vec2(1.0, 1.0));

    vec3 minimum = current;
    vec3 maximum = current;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const vec3 sampleColor =
                max(texture2D(s_source, clamp(sourceUv + vec2(float(x), float(y)) * texel,
                                              vec2(0.0, 0.0), vec2(1.0, 1.0))).rgb,
                    vec3(0.0, 0.0, 0.0));
            minimum = min(minimum, sampleColor);
            maximum = max(maximum, sampleColor);
        }
    }

    vec3 history = max(texture2D(s_taaHistory, historyUv).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 clippedHistory = clamp(history, minimum, maximum);

    const float currentDepth = texture2D(s_taaDepth, sourceUv).r;
    const float historyDepth = texture2D(s_taaDepth, historyUv).r;
    const float depthTolerance = 0.002 + currentDepth * 0.02;
    const float depthWeight = 1.0 - hbgSaturate(abs(currentDepth - historyDepth) / depthTolerance);

    const float currentLuma = hbgLuminance(current);
    const float historyLuma = hbgLuminance(history);
    const float lumaWeight = 1.0 / (1.0 + 4.0 * abs(historyLuma - currentLuma) / max(currentLuma, 0.01));

    const vec2 motionPixels = motion * u_postprocess_screenParams.xy;
    float historyWeight = u_postprocess_taaParams.y * depthWeight * lumaWeight *
                          (1.0 - hbgSaturate(length(motionPixels) * u_postprocess_taaParams.z /
                                            50.0));
    if (u_postprocess_taaParams.x < 0.5 || offScreen) {
        historyWeight = 0.0;
    }
    historyWeight = clamp(historyWeight, 0.0, 0.98);

    gl_FragColor = vec4(mix(current, clippedHistory, historyWeight), 1.0);
}
