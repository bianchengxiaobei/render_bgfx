$input v_texcoord0

#include "common/common.sc"

SAMPLER2D(s_source, 0);
SAMPLER2D(s_taaHistory, 1);
SAMPLER2D(s_taaMotionVectors, 2);
SAMPLER2D(s_taaDepth, 3);
SAMPLER2D(s_taaVelocityHistory, 4);

// HDRP TemporalAntialiasing.hlsl uses YCoCg with the 128/255 chroma bias for
// its working space. Keep the same transform so TAAU filtering matches the
// original luminance/perceptual weights.
vec3 hbgTaaRgbToYCoCg(vec3 rgb)
{
    const float bias = 128.0 / 255.0;
    return vec3(dot(rgb, vec3(0.25, 0.5, 0.25)),
                dot(rgb, vec3(0.5, 0.0, -0.5)) + bias,
                dot(rgb, vec3(-0.25, 0.5, -0.25)) + bias);
}

vec3 hbgTaaYCoCgToRgb(vec3 color)
{
    const float bias = 128.0 / 255.0;
    const float co = color.y - bias;
    const float cg = color.z - bias;
    return vec3(color.x + co - cg, color.x + cg, color.x - co - cg);
}

vec3 hbgTaaPerceptual(vec3 color)
{
    return color * (1.0 / max(color.x + 1.0, 1.0e-4));
}

vec3 hbgTaaInversePerceptual(vec3 color)
{
    return color * (1.0 / max(1.0 - color.x, 1.0e-4));
}

vec3 hbgTaaSampleWorking(vec2 uv)
{
    const vec3 rgb = max(texture2D(s_source, clamp(uv, vec2(0.0, 0.0), vec2(1.0, 1.0))).rgb, vec3(0.0, 0.0, 0.0));
    return hbgTaaPerceptual(hbgTaaRgbToYCoCg(rgb));
}

// HDRP TemporalAntialiasing.hlsl:GetClosestFragmentOffset()/GetMotionVector().
// Pick the motion vector from the closest of the four diagonal depth samples.
vec2 hbgTaaClosestFragmentOffset(vec2 uv, vec2 texel)
{
    float closestDepth = texture2D(s_taaDepth, uv).r;
    vec2 closestOffset = vec2(0.0, 0.0);
    const vec2 offsets[4] = {
        vec2(1.0, 1.0), vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(-1.0, -1.0)
    };
    for (int i = 0; i < 4; ++i) {
        const vec2 sampleUv = clamp(uv + offsets[i] * texel, vec2(0.0, 0.0), vec2(1.0, 1.0));
        const float depth = texture2D(s_taaDepth, sampleUv).r;
        if (depth < closestDepth) {
            closestDepth = depth;
            closestOffset = offsets[i];
        }
    }
    return closestOffset;
}

vec2 hbgTaaMotionVectorDepthDilated(vec2 uv, vec2 texel)
{
    const vec2 offset = hbgTaaClosestFragmentOffset(uv, texel);
    const vec2 sampleUv = clamp(uv + offset * texel, vec2(0.0, 0.0), vec2(1.0, 1.0));
    return texture2D(s_taaMotionVectors, sampleUv).xy;
}

// HDRP TemporalAntialiasing.hlsl:ModifyBlendWithMotionVectorRejection().
// lengthMV is stored pre-multiplied by 10 to match the R16F history range.
float hbgTaaModifyBlendWithVelocityRejection(float blendFactor, float lengthMV, vec2 prevUv,
                                             float speedRejectionFactor)
{
    const float prevMVLen =
        texture2D(s_taaVelocityHistory, clamp(prevUv, vec2(0.0, 0.0), vec2(1.0, 1.0))).r;
    float diff = abs(lengthMV - prevMVLen) - 0.015935382;
    const float rejection = hbgSaturate(diff * speedRejectionFactor);
    return mix(blendFactor, 0.97, rejection * rejection);
}

// HDRP TemporalAntialiasing.hlsl:HistoryBicubic5Tap()/ANTI_RINGING.
// TAAU history is at target resolution, so this is the only history resample
// that interprets the target-resolution history footprint.
vec3 hbgTaaHistoryBicubic5Tap(vec2 uv)
{
    const float sharpening = 0.35;
    const vec2 historySize = max(u_postprocess_taaHistorySize.xy, vec2(1.0, 1.0));
    const vec2 samplePos = uv * historySize;
    const vec2 tc1 = floor(samplePos - vec2(0.5, 0.5)) + vec2(0.5, 0.5);
    const vec2 f = samplePos - tc1;
    const vec2 f2 = f * f;
    const vec2 f3 = f * f2;

    const float c = sharpening;
    const vec2 w0 = -c * f3 + 2.0 * c * f2 - c * f;
    const vec2 w1 = (2.0 - c) * f3 - (3.0 - c) * f2 + vec2(1.0, 1.0);
    const vec2 w2 = -(2.0 - c) * f3 + (3.0 - 2.0 * c) * f2 + c * f;
    const vec2 w3 = c * f3 - c * f2;

    const vec2 w12 = w1 + w2;
    const vec2 tc0 = tc1 - vec2(1.0, 1.0);
    const vec2 tc3 = tc1 + vec2(1.0, 1.0);
    const vec2 tc12 = tc1 + w2 / max(w12, vec2(1.0e-5, 1.0e-5));

    const vec2 uv0 = clamp(vec2(tc12.x, tc0.y) / historySize, vec2(0.0, 0.0), vec2(1.0, 1.0));
    const vec2 uv1 = clamp(vec2(tc0.x, tc12.y) / historySize, vec2(0.0, 0.0), vec2(1.0, 1.0));
    const vec2 uv2 = clamp(tc12 / historySize, vec2(0.0, 0.0), vec2(1.0, 1.0));
    const vec2 uv3 = clamp(vec2(tc3.x, tc12.y) / historySize, vec2(0.0, 0.0), vec2(1.0, 1.0));
    const vec2 uv4 = clamp(vec2(tc12.x, tc3.y) / historySize, vec2(0.0, 0.0), vec2(1.0, 1.0));

    const vec3 s0 = max(texture2D(s_taaHistory, uv0).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 s1 = max(texture2D(s_taaHistory, uv1).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 s2 = max(texture2D(s_taaHistory, uv2).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 s3 = max(texture2D(s_taaHistory, uv3).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 s4 = max(texture2D(s_taaHistory, uv4).rgb, vec3(0.0, 0.0, 0.0));

    const float cw0 = w12.x * w0.y;
    const float cw1 = w0.x * w12.y;
    const float cw2 = w12.x * w12.y;
    const float cw3 = w3.x * w12.y;
    const float cw4 = w12.x * w3.y;
    const float weightSum = cw0 + cw1 + cw2 + cw3 + cw4;

    const vec3 filtered =
        (s0 * cw0 + s1 * cw1 + s2 * cw2 + s3 * cw3 + s4 * cw4) / max(weightSum, 1.0e-5);
    const vec3 neighborhoodMin = min(min(min(s0, s1), min(s2, s3)), s4);
    const vec3 neighborhoodMax = max(max(max(s0, s1), max(s2, s3)), s4);
    return clamp(filtered, neighborhoodMin, neighborhoodMax);
}

void main()
{
    const vec2 uv = v_texcoord0;
    const vec2 renderSize = max(u_postprocess_screenParams.xy, vec2(1.0, 1.0));
    const vec2 renderTexel = 1.0 / renderSize;

    // Fullscreen passes flip the sampled texture into their target. Keep the
    // TAA color, motion and depth lookups in the same source-texture space so
    // the resolved output preserves the post-process chain orientation.
    const vec2 sourceUv = vec2(uv.x, 1.0 - uv.y);

    // TAAU renders the history at target resolution while the scene color,
    // motion vectors and depth stay at render resolution.
    const bool upscale = u_postprocess_taaHistorySize.x > renderSize.x + 0.5 ||
                         u_postprocess_taaHistorySize.y > renderSize.y + 0.5;

    if (upscale) {
        // HDRP TemporalAntiAliasing.shader TAA_UPSAMPLE:
        // outputPixInInput = input.texcoord * _InputSize.xy - jitter.
        // The engine jitter is stored in render pixels; sourceUv is Y-flipped,
        // so the source-space mapping uses +X/-Y jitter.
        const vec2 jitter = vec2(u_postprocess_taaParams.w, u_postprocess_taaParams2.w);
        const vec2 outputPixInInput = sourceUv * renderSize + vec2(jitter.x, -jitter.y);
        const vec2 pixelFraction = outputPixInInput - floor(outputPixInInput);
        const vec2 inputToOutput = pixelFraction - vec2(0.5, 0.5);
        const vec2 currentUv = (floor(outputPixInInput) + vec2(0.5, 0.5)) / renderSize;

        // HDRP TAA_UPSAMPLE uses an 8-neighbour UPSAMPLE filter. The sample
        // weights and confidence follow TemporalAntialiasing.hlsl:
        // GetSampleWeight() and GetUpsampleConfidence().
        const vec2 offsets[9] = {
            vec2(0.0, 0.0),
            vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, -1.0),
            vec2(1.0, 1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(-1.0, -1.0)
        };

        const float filterScale = max(u_postprocess_taaHistorySize.x / renderSize.x, 1.0);
        const float filterScale2 = filterScale * filterScale;

        vec3 filtered = vec3(0.0, 0.0, 0.0);
        vec3 moment1 = vec3(0.0, 0.0, 0.0);
        vec3 moment2 = vec3(0.0, 0.0, 0.0);
        float totalWeight = 0.0;
        const vec3 centerSample = hbgTaaSampleWorking(currentUv);
        for (int i = 0; i < 9; ++i) {
            const vec3 sampleColor = i == 0 ? centerSample : hbgTaaSampleWorking(currentUv + offsets[i] * renderTexel);
            const vec2 d = offsets[i] - inputToOutput;
            const float x2 = hbgSaturate(filterScale2 * dot(d, d));
            const float f = 0.9656852 * x2 - 1.0;
            const float weight = f * f;
            filtered += sampleColor * weight;
            totalWeight += weight;
            moment1 += sampleColor;
            moment2 += sampleColor * sampleColor;
        }
        filtered *= 1.0 / max(totalWeight, 1.0e-5);

        // HDRP VarianceNeighbourhood(): mean/stddev over the central sample and
        // its 8 neighbours, with the TAAU shrink-to-0.75 on motion.
        moment1 /= 9.0;
        moment2 /= 9.0;
        const vec3 stdDev = sqrt(max(moment2 - moment1 * moment1, vec3(0.0, 0.0, 0.0)));

        // HDRP TAAU uses depth-dilated motion vectors before reprojection and
        // velocity rejection.
        const vec2 motion = hbgTaaMotionVectorDepthDilated(currentUv, renderTexel);
        const float motionPixels = length(motion * renderSize);
        const float lengthMV = length(motion) * 10.0;

        // sourceUv is vertically mirrored, so scene-space motion flips on Y.
        const vec2 prevUv = sourceUv - vec2(motion.x, -motion.y);
        const bool offScreen = any(prevUv < vec2(0.0, 0.0)) || any(prevUv > vec2(1.0, 1.0));
        const vec2 historyUv = clamp(prevUv, vec2(0.0, 0.0), vec2(1.0, 1.0));

        vec3 history = hbgTaaHistoryBicubic5Tap(historyUv);
        vec3 historyWorking = hbgTaaPerceptual(hbgTaaRgbToYCoCg(history));

        const float aggressiveHistoryLuma =
            clamp(historyWorking.x, moment1.x - stdDev.x * 0.5, moment1.x + stdDev.x * 0.5);
        const float temporalContrast =
            hbgSaturate(abs(filtered.x - aggressiveHistoryLuma) /
                        max(0.15, max(filtered.x, aggressiveHistoryLuma)));
        const float localizedAntiFlicker = mix(3.5 * 0.8, 3.5 * 2.25, hbgSaturate(1.0 - 2.0 * motionPixels));
        float stdevMultiplier = 1.5 + mix(0.0, localizedAntiFlicker, smoothstep(0.05, 0.7, temporalContrast));
        stdevMultiplier = mix(stdevMultiplier, 0.75, hbgSaturate(motionPixels / 50.0));
        stdevMultiplier = mix(0.9, stdevMultiplier, hbgSaturate(1.0 / filterScale));

        const vec3 minimum = moment1 - stdDev * stdevMultiplier;
        const vec3 maximum = moment1 + stdDev * stdevMultiplier;

        // HDRP GetClippedHistory(DIRECT_CLIP): clip towards the AABB center.
        const vec3 boxCenter = 0.5 * (maximum + minimum);
        const vec3 boxExtents = max(0.5 * (maximum - minimum), vec3(1.0e-5, 1.0e-5, 1.0e-5));
        const vec3 historyOffset = historyWorking - boxCenter;
        const vec3 unitOffset = historyOffset / boxExtents;
        const float maxUnit = max(abs(unitOffset.x), max(abs(unitOffset.y), abs(unitOffset.z)));
        const vec3 clippedHistory = maxUnit > 1.0 ? boxCenter + historyOffset / maxUnit : historyWorking;
        historyWorking = clippedHistory;

        const float currentDepth = texture2D(s_taaDepth, currentUv).r;
        const float historyDepth = texture2D(s_taaDepth, historyUv).r;
        const float depthTolerance = 0.002 + currentDepth * 0.02;
        const float depthWeight = 1.0 - hbgSaturate(abs(currentDepth - historyDepth) / depthTolerance);

        const float currentLuma = max(filtered.x, 0.0);
        const float historyLuma = max(historyWorking.x, 0.0);
        const float lumaWeight =
            1.0 / (1.0 + 4.0 * abs(historyLuma - currentLuma) / max(currentLuma, 0.01));

        // HDRP TAAU handles motion through depth-dilated motion vectors,
        // BLEND_FACTOR_MV_TUNE and velocity rejection instead of the legacy
        // per-pixel motion weight used by this project's normal TAA path.
        float historyWeight = u_postprocess_taaParams.y * depthWeight * lumaWeight;
        float blendFactor = 1.0 - historyWeight;
        // HDRP BLEND_FACTOR_MV_TUNE: trust the current frame more while moving.
        blendFactor = mix(blendFactor, hbgSaturate(2.0 * blendFactor), hbgSaturate(motionPixels / 50.0));

        // HDRP maps taaMotionVectorRejection (0..1) to a 0..250 cubic
        // multiplier inside ModifyBlendWithMotionVectorRejection().
        const float rejection = hbgSaturate(u_postprocess_taaParams.z);
        const float speedRejectionFactor = 250.0 * rejection * rejection * rejection;
        if (speedRejectionFactor > 0.0) {
            blendFactor = hbgTaaModifyBlendWithVelocityRejection(
                blendFactor, lengthMV, historyUv, speedRejectionFactor);
        }

        // HDRP GetUpsampleConfidence(BOX_REJECT): the confidence threshold is
        // 0.5 / dynamic-resolution scale = 0.5 * filter scale.
        const float confidenceThreshold = 0.5 * filterScale;
        const float confidence =
            (abs(inputToOutput.x) <= confidenceThreshold && abs(inputToOutput.y) <= confidenceThreshold) ? 1.0 : 0.0;
        blendFactor *= confidence;

        if (u_postprocess_taaParams.x < 0.5 || offScreen) {
            historyWorking = filtered;
            blendFactor = 1.0;
        }
        blendFactor = clamp(blendFactor, 0.03, 0.98);

        const vec3 finalWorking = mix(historyWorking, filtered, blendFactor);
        const vec3 finalColor = hbgTaaYCoCgToRgb(hbgTaaInversePerceptual(finalWorking));
        gl_FragData[0] = vec4(max(finalColor, vec3(0.0, 0.0, 0.0)), 1.0);
        gl_FragData[1] = vec4(lengthMV, 0.0, 0.0, 0.0);
        return;
    }

    const vec2 texel = u_postprocess_screenParams.zw;
    const vec2 currentUv = sourceUv;
    const vec3 current = max(texture2D(s_source, currentUv).rgb, vec3(0.0, 0.0, 0.0));

    const vec2 motion = texture2D(s_taaMotionVectors, currentUv).xy;
    // sourceUv is vertically mirrored, so scene-space motion flips on Y.
    const vec2 prevUv = sourceUv - vec2(motion.x, -motion.y);
    const bool offScreen = any(prevUv < vec2(0.0, 0.0)) || any(prevUv > vec2(1.0, 1.0));
    const vec2 historyUv = clamp(prevUv, vec2(0.0, 0.0), vec2(1.0, 1.0));

    vec3 minimum = current;
    vec3 maximum = current;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const vec3 sampleColor =
                max(texture2D(s_source, clamp(currentUv + vec2(float(x), float(y)) * texel,
                                              vec2(0.0, 0.0), vec2(1.0, 1.0))).rgb,
                    vec3(0.0, 0.0, 0.0));
            minimum = min(minimum, sampleColor);
            maximum = max(maximum, sampleColor);
        }
    }

    vec3 history = max(texture2D(s_taaHistory, historyUv).rgb, vec3(0.0, 0.0, 0.0));
    const vec3 clippedHistory = clamp(history, minimum, maximum);

    const float currentDepth = texture2D(s_taaDepth, currentUv).r;
    const float historyDepth = texture2D(s_taaDepth, historyUv).r;
    const float depthTolerance = 0.002 + currentDepth * 0.02;
    const float depthWeight = 1.0 - hbgSaturate(abs(currentDepth - historyDepth) / depthTolerance);

    const float currentLuma = hbgLuminance(current);
    const float historyLuma = hbgLuminance(history);
    const float lumaWeight = 1.0 / (1.0 + 4.0 * abs(historyLuma - currentLuma) / max(currentLuma, 0.01));

    const vec2 motionPixels = motion * renderSize;
    float historyWeight = u_postprocess_taaParams.y * depthWeight * lumaWeight *
                          (1.0 - hbgSaturate(length(motionPixels) * u_postprocess_taaParams.z /
                                            50.0));
    if (u_postprocess_taaParams.x < 0.5 || offScreen) {
        historyWeight = 0.0;
    }
    historyWeight = clamp(historyWeight, 0.0, 0.98);

    gl_FragData[0] = vec4(mix(current, clippedHistory, historyWeight), 1.0);
    gl_FragData[1] = vec4(0.0, 0.0, 0.0, 0.0);
}
