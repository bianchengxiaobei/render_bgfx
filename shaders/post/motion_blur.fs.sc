$input v_texcoord0

#include "common/common.sc"
#include "postprocess/dof.sc"

SAMPLER2D(s_source, 0);
SAMPLER2D(s_motionBlurVectors, 1);
SAMPLER2D(s_motionBlurDepth, 2);

vec2 hbgMotionBlurCameraDelta(vec2 sourceUv, float depth)
{
    const vec4 worldPosition = mul(u_global_invViewProj, vec4(sourceUv * 2.0 - 1.0, depth, 1.0));
    const vec4 currentClip = mul(u_global_nonJitteredViewProj, worldPosition);
    const vec4 previousClip = mul(u_global_prevViewProj, worldPosition);
    const vec2 currentNdc = currentClip.xy / currentClip.w;
    const vec2 previousNdc = previousClip.xy / previousClip.w;
    return (currentNdc - previousNdc) * 0.5;
}

vec2 hbgMotionBlurClampVelocity(vec2 motionNdc)
{
    motionNdc *= u_postprocess_motionBlurParams.x;
    const float maximumNdc = max(u_postprocess_motionBlurParams.y, 0.0);
    const float motionLength = length(motionNdc);
    if (motionLength > maximumNdc && motionLength > 1.0e-6) {
        motionNdc *= maximumNdc / motionLength;
    }
    return motionNdc;
}

void main()
{
    const vec2 sourceUv = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y);
    const vec2 texel = u_postprocess_screenParams.zw;
    const vec3 centerColor = max(texture2D(s_source, sourceUv).rgb, vec3(0.0, 0.0, 0.0));
    const float centerRawDepth = texture2D(s_motionBlurDepth, sourceUv).r;
    const float centerDepth = hbgDofLinearEyeDepth(centerRawDepth, sourceUv);

    vec2 centerMotion = texture2D(s_motionBlurVectors, sourceUv).xy;
    const vec2 centerCameraMotion =
        u_postprocess_motionBlurParams2.z < 0.5 ? hbgMotionBlurCameraDelta(sourceUv, centerRawDepth)
                                                : vec2(0.0, 0.0);
    centerMotion -= centerCameraMotion;

    // A small local maximum approximates HDRP's tile-neighborhood max without
    // adding a tile pass. It lets a moving foreground contribute to nearby
    // static background pixels while keeping the fixed cross-backend tap
    // pattern deterministic.
    vec2 maxMotion = centerMotion;
    const float neighborhoodRadius = max(u_postprocess_motionBlurParams2.w, 1.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const vec2 sampleUv =
                clamp(sourceUv + vec2(float(x), float(y)) * texel * neighborhoodRadius, vec2(0.0, 0.0), vec2(1.0, 1.0));
            vec2 sampleMotion = texture2D(s_motionBlurVectors, sampleUv).xy;
            if (u_postprocess_motionBlurParams2.z < 0.5) {
                const float sampleDepth = texture2D(s_motionBlurDepth, sampleUv).r;
                sampleMotion -= hbgMotionBlurCameraDelta(sampleUv, sampleDepth);
            }
            if (dot(sampleMotion, sampleMotion) > dot(maxMotion, maxMotion)) {
                maxMotion = sampleMotion;
            }
        }
    }

    const float screenMagnitude = max(u_postprocess_motionBlurParams2.x, 1.0);
    const vec2 scaledCenter = hbgMotionBlurClampVelocity(centerMotion);
    const vec2 scaledMax = hbgMotionBlurClampVelocity(maxMotion);
    const float maxMotionPixels = length(scaledMax) * screenMagnitude;
    const float minMotionPixels = max(u_postprocess_motionBlurParams.z, 0.0);
    if (maxMotionPixels <= max(minMotionPixels, 0.5) || length(scaledMax) <= 1.0e-6) {
        gl_FragColor = vec4(centerColor, 1.0);
        return;
    }

    uint actualSampleCount = min(uint(max(2.0, floor(maxMotionPixels))),
                                 uint(max(u_postprocess_motionBlurParams.w, 2.0)));
    if ((actualSampleCount & 1u) != 0u) {
        ++actualSampleCount;
    }
    actualSampleCount = max(actualSampleCount, 2u);

    const float invSampleCount = 1.0 / float(actualSampleCount);
    const uint stepCount = actualSampleCount / 2u;
    const vec2 direction = vec2(scaledMax.x, -scaledMax.y) * 0.5;
    const float depthScale = max(u_postprocess_motionBlurParams2.y, 0.0);

    vec3 accumulation = vec3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    for (uint i = 0u; i < 16u; ++i) {
        if (i >= stepCount) {
            break;
        }
        const float offset = (float(i) + 0.5) * invSampleCount;
        const vec2 offsetUv = direction * offset;
        const vec2 forwardUv = clamp(sourceUv + offsetUv, vec2(0.0, 0.0), vec2(1.0, 1.0));
        const vec2 backwardUv = clamp(sourceUv - offsetUv, vec2(0.0, 0.0), vec2(1.0, 1.0));

        const vec3 forwardColor = max(texture2DLod(s_source, forwardUv, 0.0).rgb, vec3(0.0, 0.0, 0.0));
        const vec3 backwardColor = max(texture2DLod(s_source, backwardUv, 0.0).rgb, vec3(0.0, 0.0, 0.0));
        const float forwardDepth =
            hbgDofLinearEyeDepth(texture2DLod(s_motionBlurDepth, forwardUv, 0.0).r, forwardUv);
        const float backwardDepth =
            hbgDofLinearEyeDepth(texture2DLod(s_motionBlurDepth, backwardUv, 0.0).r, backwardUv);

        const float forwardWeight = clamp(0.5 - depthScale * (forwardDepth - centerDepth), 0.0, 1.0);
        const float backwardWeight = clamp(0.5 - depthScale * (backwardDepth - centerDepth), 0.0, 1.0);
        accumulation += forwardColor * forwardWeight + backwardColor * backwardWeight;
        weightSum += forwardWeight + backwardWeight;
    }

    const vec3 topLayerColor =
        weightSum > 1.0e-4 ? accumulation / weightSum : centerColor;
    const float topLayerAlpha = weightSum * invSampleCount;
    const vec3 backLayerColor = mix(centerColor, topLayerColor, topLayerAlpha);
    const float layerBlendWeight =
        clamp(length(scaledCenter) / max(length(scaledMax), 1.0e-6), 0.0, 1.0);
    gl_FragColor = vec4(mix(backLayerColor, topLayerColor, layerBlendWeight), 1.0);
}
