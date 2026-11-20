#ifndef HBG_SHADOWS_SC
#define HBG_SHADOWS_SC

#include "common/common.sc"
#include "bgfx_compute.sh"

SAMPLER2D(s_shadowMap, 6);
SAMPLER2D(s_shadowMapPunctual, 9);

#ifdef HBG_SHADOW_POINT_SAMPLER
#define HBG_SHADOW_AREA_SAMPLER
#endif
#ifdef HBG_SHADOW_POINT_SAMPLER
USAMPLER2D(s_shadowMapPoint, 12);
#endif
#ifdef HBG_SHADOW_AREA_SAMPLER
SAMPLER2D(s_shadowMapPointPunctual, 13);
#endif

float hbgShadowMapCompare(vec2 uv, float depth)
{
    return texture2DLod(s_shadowMap, uv, 0.0).x >= depth ? 1.0 : 0.0;
}

float hbgShadowPunctualCompare(vec2 uv, float depth)
{
    return texture2DLod(s_shadowMapPunctual, uv, 0.0).x >= depth ? 1.0 : 0.0;
}

// HDRP defaults: EVSM exponent 15 (base-two), leak bias 0, variance bias 1e-5.
// The project has no moment atlas yet, so this v1 gathers a local 3x3 moment
// window from the existing raw shadow depth instead of the HDRP blurred atlas.
float hbgShadowMomentRawDepth(vec2 uv, int space)
{
    if (space == 1)
    {
        return texture2DLod(s_shadowMapPunctual, uv, 0.0).x;
    }
#ifdef HBG_SHADOW_AREA_SAMPLER
    if (space == 2)
    {
        return texture2DLod(s_shadowMapPointPunctual, uv, 0.0).x;
    }
#endif
#ifdef HBG_SHADOW_POINT_SAMPLER
    if (space == 0)
    {
        return texture2DLod(s_shadowMap, uv, 0.0).x;
    }
#endif
    return 1.0;
}

float hbgShadowEvsmWarp(float depth, float exponent)
{
    return exp2(exponent * (2.0 * depth - 1.0));
}

// HDRP SampleShadow_VSM_1tap reads a blurred moment atlas. This v1 uses the
// same Chebyshev test but gathers the first two raw-depth moments locally.
float hbgSampleVsm(vec2 atlasUV, float depth, vec2 texel, vec2 uvMin, vec2 uvMax, int space)
{
    const float lightLeakBias = 0.0;
    const float varianceBias = 1.0e-5;
    float moment1 = 0.0;
    float moment2 = 0.0;
    float weightSum = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const vec2 uv = clamp(atlasUV + vec2(float(x), float(y)) * texel * 1.5, uvMin, uvMax);
            const float rawDepth = hbgShadowMomentRawDepth(uv, space);
            const float weight = (x == 0 && y == 0) ? 2.0 : 1.0;
            moment1 += rawDepth * weight;
            moment2 += rawDepth * rawDepth * weight;
            weightSum += weight;
        }
    }
    moment1 /= weightSum;
    moment2 /= weightSum;

    const float variance = max(moment2 - moment1 * moment1, varianceBias);
    const float delta = depth - moment1;
    float probability = variance / max(variance + delta * delta, 1.0e-20);
    probability = hbgSaturate((probability - lightLeakBias) / max(1.0 - lightLeakBias, 1.0e-6));
    return max(probability, depth <= moment1 ? 1.0 : 0.0);
}

float hbgSampleEvsm(vec2 atlasUV, float depth, vec2 texel, vec2 uvMin, vec2 uvMax, int space)
{
    const float exponent = 21.640392f;
    const float lightLeakBias = 0.0;
    const float varianceBias = 1.0e-5;
    float moment1 = 0.0;
    float moment2 = 0.0;
    float weightSum = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const vec2 uv = clamp(atlasUV + vec2(float(x), float(y)) * texel * 1.5, uvMin, uvMax);
            const float rawDepth = hbgShadowMomentRawDepth(uv, space);
            const float warped = hbgShadowEvsmWarp(rawDepth, exponent);
            const float weight = (x == 0 && y == 0) ? 2.0 : 1.0;
            moment1 += warped * weight;
            moment2 += warped * warped * weight;
            weightSum += weight;
        }
    }
    moment1 /= weightSum;
    moment2 /= weightSum;

    const float warpedDepth = hbgShadowEvsmWarp(depth, exponent);
    const float depthScale = exponent * warpedDepth;
    const float minVariance = depthScale * depthScale * varianceBias;
    const float variance = max(moment2 - moment1 * moment1, minVariance);
    const float delta = warpedDepth - moment1;
    float probability = variance / max(variance + delta * delta, 1.0e-20);
    probability = hbgSaturate((probability - lightLeakBias) / max(1.0 - lightLeakBias, 1.0e-6));
    return max(probability, warpedDepth <= moment1 ? 1.0 : 0.0);
}

float hbgMsmFallback(float mean, float variance, float depth)
{
    const float minVariance = 1.0e-5;
    const float safeVariance = max(variance, minVariance);
    const float delta = depth - mean;
    const float probability = safeVariance / max(safeVariance + delta * delta, 1.0e-20);
    return max(probability, depth <= mean ? 1.0 : 0.0);
}

float hbgMsmDelta3(float z0, float z1, float z2, float b0, float b1)
{
    float switch0;
    float switch1;
    float switch2;
    float switch3;
    if (z2 < z0)
    {
        switch0 = z1;
        switch1 = z0;
        switch2 = 1.0;
        switch3 = 1.0;
    }
    else if (z1 < z0)
    {
        switch0 = z0;
        switch1 = z1;
        switch2 = 0.0;
        switch3 = 1.0;
    }
    else
    {
        switch0 = 0.0;
        switch1 = 0.0;
        switch2 = 0.0;
        switch3 = 0.0;
    }

    const float denominator = (z2 - switch1) * (z0 - z1);
    if (abs(denominator) < 1.0e-8)
    {
        return 1.0;
    }
    const float quotient =
        (switch0 * z2 - b0 * (switch0 + z2) + b1) / denominator;
    const float attenuation = hbgSaturate(switch2 + switch3 * quotient);
    return hbgSaturate(1.0 - attenuation);
}

float hbgMsmDelta4(float z0, float zFree, float b0, float b1, float b2, float b3)
{
    const float denominator = z0 * (z0 - zFree);
    const float weightDenominator = zFree - (z0 > zFree ? 1.0 : 0.0);
    if (abs(denominator) < 1.0e-8 || abs(weightDenominator) < 1.0e-8)
    {
        return 1.0;
    }

    const float w1Factor = z0 > zFree ? 1.0 : 0.0;
    const float attenuation = hbgSaturate(
        (b1 - b0 + (b2 - b0 - (zFree + 1.0) * (b1 - b0)) * (zFree - w1Factor - z0) /
                        denominator) /
                weightDenominator +
            1.0 - b0);
    return hbgSaturate(1.0 - attenuation);
}

#ifdef HBG_SHADOW_POINT_SAMPLER
// HDRP HDIMS.hlsl: optimized moments are integrated in a summed-area table
// (MomentShadows.compute) and sampled with a 4-moment rectangle integral.
// This path is directional-only, matching HDRP's improved moment shadows.
// Stage 12 is an RGBA32UI texture; its integer SAT is read without filtering
// exactly like HDRP's Texture2D<uint4> _SummedAreaTableInputInt.
float hbgImsFixedPrecision(float kernelSize)
{
    const float maxKernelSize = max(ceil(1.0 + max(kernelSize, 1.0)), 1.0);
    return 4294967295.0 / max(maxKernelSize * maxKernelSize, 1.0);
}

uvec4 hbgImsLoadSat(ivec2 pixel, ivec2 minPixel, ivec2 maxPixel)
{
    if (any(lessThan(pixel, minPixel)) || any(greaterThan(pixel, maxPixel)))
    {
        return uvec4(0u, 0u, 0u, 0u);
    }
    return texelFetch(s_shadowMapPoint, pixel, 0);
}

vec4 hbgImsIntegerAverage(vec2 uvMin, vec2 uvMax, vec2 atlasSize, ivec2 minPixel,
                          ivec2 maxPixel, float invFixedPrecision)
{
    const ivec2 top = clamp(ivec2(uvMin * atlasSize) - ivec2(1, 1),
                            minPixel - ivec2(1, 1), maxPixel);
    const ivec2 bottom = clamp(ivec2(uvMax * atlasSize) - ivec2(1, 1),
                               minPixel - ivec2(1, 1), maxPixel);
    const ivec2 size = bottom - top;
    if (size.x <= 0 || size.y <= 0)
    {
        return vec4(0.0, 0.0, 0.0, 0.0);
    }
    const uvec4 sum = hbgImsLoadSat(top, minPixel, maxPixel) +
                      hbgImsLoadSat(bottom, minPixel, maxPixel) -
                      hbgImsLoadSat(ivec2(top.x, bottom.y), minPixel, maxPixel) -
                      hbgImsLoadSat(ivec2(bottom.x, top.y), minPixel, maxPixel);
    return vec4(sum) * invFixedPrecision / float(size.x * size.y);
}

vec4 hbgImsRectangleAverage(vec2 leftTop, vec2 rightBottom, vec2 atlasSize,
                            ivec2 minPixel, ivec2 maxPixel, float invFixedPrecision)
{
    const vec2 texel = 1.0 / atlasSize;
    leftTop -= texel;
    rightBottom -= texel;
    const vec2 leftPixel = leftTop * atlasSize;
    const vec2 rightPixel = rightBottom * atlasSize;
    const vec2 leftFloor = floor(leftPixel);
    const vec2 rightFloor = floor(rightPixel);
    const ivec2 left = ivec2(leftFloor);
    const ivec2 right = ivec2(rightFloor);
    const vec2 leftFactor = vec2(1.0, 1.0) - (leftPixel - leftFloor);
    const vec2 rightFactor = rightPixel - rightFloor;

    uvec4 samples[2][2][2][2];
    for (int x = 0; x < 2; ++x)
    {
        const int pixelX = (x == 0) ? left.x : right.x;
        for (int y = 0; y < 2; ++y)
        {
            const int pixelY = (y == 0) ? left.y : right.y;
            for (int z = 0; z < 2; ++z)
            {
                for (int w = 0; w < 2; ++w)
                {
                    samples[x][y][z][w] =
                        hbgImsLoadSat(ivec2(pixelX + z, pixelY + w), minPixel, maxPixel);
                }
            }
        }
    }

    uvec4 corner[2][2];
    for (int x = 0; x < 2; ++x)
    {
        for (int y = 0; y < 2; ++y)
        {
            corner[x][y] = samples[x][y][0][0] + samples[x][y][1][1] -
                           samples[x][y][1][0] - samples[x][y][0][1];
        }
    }

    const uvec4 rightEdge = samples[1][0][0][1] + samples[1][1][1][0] -
                           samples[1][0][1][1] - samples[1][1][0][0];
    const uvec4 topEdge = samples[0][0][1][0] + samples[1][0][0][1] -
                         samples[0][0][1][1] - samples[1][0][0][0];
    const uvec4 leftEdge = samples[0][0][0][1] + samples[0][1][1][0] -
                          samples[0][0][1][1] - samples[0][1][0][0];
    const uvec4 bottomEdge = samples[0][1][1][0] + samples[1][1][0][1] -
                            samples[0][1][1][1] - samples[1][1][0][0];
    const uvec4 center = samples[0][0][1][1] + samples[1][1][0][0] -
                        samples[0][1][1][0] - samples[1][0][0][1];

    vec4 integral = vec4(center);
    integral += vec4(corner[0][0]) * (leftFactor.x * leftFactor.y);
    integral += vec4(corner[0][1]) * (leftFactor.x * rightFactor.y);
    integral += vec4(corner[1][0]) * (rightFactor.x * leftFactor.y);
    integral += vec4(corner[1][1]) * (rightFactor.x * rightFactor.y);
    integral += vec4(rightEdge) * rightFactor.x;
    integral += vec4(topEdge) * leftFactor.y;
    integral += vec4(leftEdge) * leftFactor.x;
    integral += vec4(bottomEdge) * rightFactor.y;

    const vec2 size = rightPixel - leftPixel;
    if (size.x <= 0.0 || size.y <= 0.0)
    {
        return vec4(0.0, 0.0, 0.0, 0.0);
    }
    return integral * invFixedPrecision / (size.x * size.y);
}

vec4 hbgImsCanonical(vec4 optimized)
{
    vec4 b;
    b.x = (optimized.x - 0.5) * (-1.0 / 3.0) + (optimized.z - 0.5) * sqrt(3.0);
    b.z = (optimized.x - 0.5) * (-0.75) + (optimized.z - 0.5) * (0.75 * sqrt(3.0));
    b.y = optimized.y * 0.125 + optimized.w;
    b.w = optimized.y * (-0.125) + optimized.w;
    return mix(b, vec4(0.0, 0.628, 0.0, 0.628), 6.0e-7);
}

void hbgImsAverageBlocker(out float averageBlockerDepth, out float blockerIntensity, float fragmentDepth,
                         vec4 b)
{
    averageBlockerDepth = fragmentDepth;
    blockerIntensity = -1.0;

    const float z0 = fragmentDepth;
    const float l21D11 = -b.x * b.y + b.z;
    const float d11 = -b.x * b.x + b.y;
    const float squaredDepthVariance = -b.y * b.y + b.w;
    const float d22D11 = squaredDepthVariance * d11 - l21D11 * l21D11;
    if (abs(d11) < 1.0e-12 || abs(d22D11) < 1.0e-20)
    {
        return;
    }

    const float invD11 = 1.0 / d11;
    const float l21 = l21D11 * invD11;
    float c1 = z0 - b.x;
    float c2 = z0 * z0 - b.y - l21 * c1;
    c1 *= invD11;
    c2 *= d11 / d22D11;
    c1 -= l21 * c2;
    if (abs(c2) < 1.0e-12)
    {
        return;
    }

    const float p = c1 / c2;
    const float q = (1.0 - c1 * b.x - c2 * b.y) / c2;
    const float discriminant = p * p * 0.25 - q;
    if (!(discriminant >= 0.0))
    {
        return;
    }

    const float root = sqrt(max(discriminant, 0.0));
    const float z1 = -0.5 * p - root;
    const float z2 = -0.5 * p + root;
    const float w0 = (z1 * z2 - b.x * (z1 + z2) + b.y) / ((z0 - z1) * (z0 - z2));
    const float w1 = (z0 * z2 - b.x * (z0 + z2) + b.y) / ((z2 - z1) * (z0 - z1));
    const float w2 = 1.0 - w0 - w1;
    const float blockerSum = (z1 < z0 ? w1 : 0.0) + (z2 < z0 ? w2 : 0.0);
    if (blockerSum <= 0.99)
    {
        return;
    }
    const float fullyLitBias = 1.0e-3;
    averageBlockerDepth = clamp((fullyLitBias * z0 + (z1 < z0 ? w1 * z1 : 0.0) +
                                 (z2 < z0 ? w2 * z2 : 0.0)) /
                                    (fullyLitBias + blockerSum),
                                -1.0, 1.0);
    blockerIntensity = 1.0;
}

float hbgImsUnboundedVisibility(vec4 b, float fragmentDepth)
{
    const float z0 = fragmentDepth;
    const float l21D11 = -b.x * b.y + b.z;
    const float d11 = -b.x * b.x + b.y;
    const float squaredDepthVariance = -b.y * b.y + b.w;
    const float d22D11 = squaredDepthVariance * d11 - l21D11 * l21D11;
    if (abs(d11) < 1.0e-12 || abs(d22D11) < 1.0e-20)
    {
        return 1.0;
    }

    const float invD11 = 1.0 / d11;
    const float l21 = l21D11 * invD11;
    const float d22 = d22D11 * invD11;
    if (abs(d22) < 1.0e-20)
    {
        return 1.0;
    }
    const float invD22 = 1.0 / d22;
    float c1 = z0 - b.x;
    float c2 = z0 * z0 - b.y - l21 * c1;
    c1 *= invD11;
    c2 *= invD22;
    c1 -= l21 * c2;
    if (abs(c2) < 1.0e-12)
    {
        return 1.0;
    }

    const float p = c1 / c2;
    const float q = (1.0 - c1 * b.x - c2 * b.y) / c2;
    const float discriminant = p * p * 0.25 - q;
    if (!(discriminant >= 0.0))
    {
        return 1.0;
    }
    const float root = sqrt(max(discriminant, 0.0));
    const float z1 = -0.5 * p - root;
    const float z2 = -0.5 * p + root;

    float switch0;
    float switch1;
    float switch2;
    float switch3;
    if (z2 < z0)
    {
        switch0 = z1;
        switch1 = z0;
        switch2 = 1.0;
        switch3 = 1.0;
    }
    else if (z1 < z0)
    {
        switch0 = z0;
        switch1 = z1;
        switch2 = 0.0;
        switch3 = 1.0;
    }
    else
    {
        switch0 = 0.0;
        switch1 = 0.0;
        switch2 = 0.0;
        switch3 = 0.0;
    }

    const float denominator = (z2 - switch1) * (z0 - z1);
    if (abs(denominator) < 1.0e-8)
    {
        return 1.0;
    }
    const float quotient = (switch0 * z2 - b.x * (switch0 + z2) + b.y) / denominator;
    return 1.0 - hbgSaturate(switch2 + switch3 * quotient);
}

vec4 hbgLoadShadowImsParams(int request);

float hbgSampleIms(vec2 atlasUV, float depth, vec2 texel, vec4 atlasRect, int request)
{
    const vec4 imsParams = hbgLoadShadowImsParams(request);
    const vec2 atlasSize = 1.0 / max(texel, vec2(1.0e-8, 1.0e-8));
    const ivec2 minPixel = ivec2(atlasRect.zw * atlasSize + vec2(0.5, 0.5));
    const ivec2 slotSize = ivec2(atlasRect.xy * atlasSize + vec2(0.5, 0.5));
    const ivec2 maxPixel = minPixel + max(slotSize, ivec2(1, 1)) - ivec2(1, 1);
    const vec2 slotMin = atlasRect.zw;
    const vec2 slotMax = slotMin + atlasRect.xy;
    const float kernelSize = max(imsParams.x, 1.0);
    const float invFixedPrecision = 1.0 / hbgImsFixedPrecision(kernelSize);
    const vec2 kernelMin = 1.0 / atlasSize;
    const vec2 kernelMax = kernelSize * 0.5 / atlasSize;
    const float kernelScale = atlasSize.x / max(0.5 * kernelSize - 0.5, 0.5);
    const vec2 searchMin = max(atlasUV - kernelMax - 0.5 * texel, slotMin);
    const vec2 searchMax = min(atlasUV + kernelMax + 0.5 * texel, slotMax);
    const vec4 averageMoment =
        hbgImsIntegerAverage(searchMin, searchMax, atlasSize, minPixel, maxPixel,
                             invFixedPrecision);
    const vec4 b = hbgImsCanonical(averageMoment);
    const float fragmentDepth = (1.0 - depth) * 2.0 - 1.0;

    float averageBlockerDepth;
    float blockerIntensity;
    hbgImsAverageBlocker(averageBlockerDepth, blockerIntensity, fragmentDepth, b);
    if (blockerIntensity < 0.99)
    {
        return 1.0;
    }

    const vec2 penumbra =
        max(kernelMin, min(kernelMax, imsParams.yz * abs(fragmentDepth - averageBlockerDepth)));
    const float estimatedDepthBias =
        imsParams.w * clamp(penumbra.x * kernelScale, 0.1, 1.0);
    const vec2 filterMin = clamp(atlasUV - penumbra, slotMin, slotMax);
    const vec2 filterMax = clamp(atlasUV + penumbra, slotMin, slotMax);
    const vec4 optimizedMoment =
        hbgImsRectangleAverage(filterMin, filterMax, atlasSize, minPixel, maxPixel,
                               invFixedPrecision);
    const vec4 biasedMoment = hbgImsCanonical(optimizedMoment);
    return hbgImsUnboundedVisibility(biasedMoment, fragmentDepth - estimatedDepthBias);
}
#endif

// HDRP's 1-tap MSM samples a blurred 4-moment atlas and solves the two
// Dirac-delta approximations. This v1 gathers the four raw-depth moments over
// the same local 3x3 window; it does not implement the HDRP moment atlas/SAT.
float hbgSampleMsm(vec2 atlasUV, float depth, vec2 texel, vec2 uvMin, vec2 uvMax, int space)
{
    const float momentBias = 6.0e-7;
    float moment0 = 0.0;
    float moment1 = 0.0;
    float moment2 = 0.0;
    float moment3 = 0.0;
    float weightSum = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const vec2 uv = clamp(atlasUV + vec2(float(x), float(y)) * texel * 1.5, uvMin, uvMax);
            const float d = hbgShadowMomentRawDepth(uv, space);
            const float d2 = d * d;
            const float weight = (x == 0 && y == 0) ? 2.0 : 1.0;
            moment0 += d * weight;
            moment1 += d2 * weight;
            moment2 += d2 * d * weight;
            moment3 += d2 * d2 * weight;
            weightSum += weight;
        }
    }
    moment0 /= weightSum;
    moment1 /= weightSum;
    moment2 /= weightSum;
    moment3 /= weightSum;

    const float mean = moment0;
    const float variance = moment1 - mean * mean;
    const float b0 = lerp(moment0, 0.5, momentBias);
    const float b1 = lerp(moment1, 0.5, momentBias);
    const float b2 = lerp(moment2, 0.5, momentBias);
    const float b3 = lerp(moment3, 0.5, momentBias);

    const float L32D22 = -b0 * b1 + b2;
    const float D22 = -b0 * b0 + b1;
    const float squaredDepthVariance = -b1 * b1 + b3;
    const float D33D22 = squaredDepthVariance * D22 - L32D22 * L32D22;
    if (abs(D22) < 1.0e-8 || abs(D33D22) < 1.0e-12)
    {
        return hbgMsmFallback(mean, variance, depth);
    }

    const float inverseD22 = 1.0 / D22;
    const float L32 = L32D22 * inverseD22;
    float c0 = 1.0;
    float c1 = depth - b0;
    float c2 = depth * depth - b1 - L32 * c1;
    c1 *= inverseD22;
    c2 *= D22 / D33D22;
    c1 -= L32 * c2;
    c0 -= c1 * b0 + c2 * b1;

    if (abs(c2) < 1.0e-8)
    {
        return hbgMsmFallback(mean, variance, depth);
    }

    const float p = c1 / c2;
    const float q = c0 / c2;
    const float discriminant = p * p * 0.25 - q;
    if (!(discriminant >= 0.0))
    {
        return hbgMsmFallback(mean, variance, depth);
    }

    const float root = sqrt(max(discriminant, 0.0));
    const float z1 = -p * 0.5 - root;
    const float z2 = -p * 0.5 + root;
    if (z1 < 0.0 || z2 > 1.0)
    {
        const float freeDenominator = (b1 - b0) * depth + b1 - b2;
        if (abs(freeDenominator) < 1.0e-8)
        {
            return hbgMsmFallback(mean, variance, depth);
        }
        const float zFree = ((b2 - b1) * depth + b2 - b3) / freeDenominator;
        return hbgMsmDelta4(depth, zFree, b0, b1, b2, b3);
    }
    return hbgMsmDelta3(depth, z1, z2, b0, b1);
}

// HDRP keeps HDShadowData in StructuredBuffer<HDShadowData>; stage 8 uses the
// same float4 texels per request, loaded as one vector per fetch.
#define HBG_SHADOW_CONTEXT_TEXELS 10
#if BGFX_SHADER_LANGUAGE_GLSL
layout(std430, binding = 8) readonly buffer HbgShadowContextBuffer
{
    vec4 s_shadowContext[];
};
#elif BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV || BGFX_SHADER_LANGUAGE_WGSL
StructuredBuffer<float4> s_shadowContext : register(t8);
#else
Buffer<float4> s_shadowContext : register(t8);
#endif

vec4 hbgLoadShadowContextTexel(int texel, int request)
{
    return s_shadowContext[request * HBG_SHADOW_CONTEXT_TEXELS + texel];
}

mat4 hbgLoadShadowMatrix(int request)
{
    return mat4(hbgLoadShadowContextTexel(0, request).xyzw,
                hbgLoadShadowContextTexel(1, request).xyzw,
                hbgLoadShadowContextTexel(2, request).xyzw,
                hbgLoadShadowContextTexel(3, request).xyzw);
}

vec4 hbgLoadShadowAtlasRect(int request)
{
    return hbgLoadShadowContextTexel(4, request);
}

vec4 hbgLoadShadowRequestParams(int request)
{
    return hbgLoadShadowContextTexel(5, request);
}

vec4 hbgLoadShadowRequestInfo(int request)
{
    return hbgLoadShadowContextTexel(6, request);
}

vec4 hbgLoadShadowFilterParams(int request)
{
    return hbgLoadShadowContextTexel(7, request);
}

vec4 hbgLoadShadowFade(int request)
{
    return hbgLoadShadowContextTexel(8, request);
}

vec4 hbgLoadShadowPcssParams(int request)
{
    return hbgLoadShadowContextTexel(9, request);
}

// For IMS requests texel 9 stores the per-cascade soft-shadow parameters
// instead of the PCSS blocker/filter parameters. This mirrors HDRP's
// per-HDShadowData shadow parameters and avoids a global first-cascade value.
vec4 hbgLoadShadowImsParams(int request)
{
    return hbgLoadShadowContextTexel(9, request);
}

void hbgTentAreas3(float offset, out vec4 area, out vec4 uncut)
{
    const float offset01SquaredHalved = (offset + 0.5) * (offset + 0.5) * 0.5;
    uncut.x = area.x = offset01SquaredHalved - offset;
    uncut.w = area.w = offset01SquaredHalved;

    uncut.y = 1.5 - offset - 0.5;
    const float clampedOffsetLeft = min(offset, 0.0);
    area.y = uncut.y - clampedOffsetLeft * clampedOffsetLeft;

    uncut.z = 1.5 + offset - 0.5;
    const float clampedOffsetRight = max(offset, 0.0);
    area.z = uncut.z - clampedOffsetRight * clampedOffsetRight;
}

void hbgTentWeights3(float offset, out vec4 weights)
{
    vec4 area;
    vec4 uncut;
    hbgTentAreas3(offset, area, uncut);
    weights = area * 0.44444;
}

void hbgTentWeights5(float offset, out vec3 weightsA, out vec3 weightsB)
{
    vec4 area;
    vec4 uncut;
    hbgTentAreas3(offset, area, uncut);
    weightsA.x = 0.16 * area.x;
    weightsA.y = 0.16 * uncut.y;
    weightsA.z = 0.16 * (area.y + 1.0);
    weightsB.x = 0.16 * (area.z + 1.0);
    weightsB.y = 0.16 * uncut.z;
    weightsB.z = 0.16 * area.w;
}

void hbgTentWeights7(float offset, out vec4 weightsA, out vec4 weightsB)
{
    vec4 area;
    vec4 uncut;
    hbgTentAreas3(offset, area, uncut);
    weightsA.x = 0.081632 * area.x;
    weightsA.y = 0.081632 * uncut.y;
    weightsA.z = 0.081632 * (uncut.y + 1.0);
    weightsA.w = 0.081632 * (area.y + 2.0);
    weightsB.x = 0.081632 * (area.z + 2.0);
    weightsB.y = 0.081632 * (uncut.z + 1.0);
    weightsB.z = 0.081632 * uncut.z;
    weightsB.w = 0.081632 * area.w;
}

void hbgTentSamples3(vec2 coord, vec2 texel, out float weights[4], out vec2 uvs[4])
{
    const vec2 tentCenterInTexelSpace = coord / texel;
    const vec2 centerOfFetchesInTexelSpace = floor(tentCenterInTexelSpace + 0.5);
    const vec2 offsetFromTentCenter = tentCenterInTexelSpace - centerOfFetchesInTexelSpace;

    vec4 weightsU;
    vec4 weightsV;
    hbgTentWeights3(offsetFromTentCenter.x, weightsU);
    hbgTentWeights3(offsetFromTentCenter.y, weightsV);

    const vec2 fetchesWeightsU = weightsU.xz + weightsU.yw;
    const vec2 fetchesWeightsV = weightsV.xz + weightsV.yw;
    vec2 fetchesOffsetsU = weightsU.yw / fetchesWeightsU + vec2(-1.5, 0.5);
    vec2 fetchesOffsetsV = weightsV.yw / fetchesWeightsV + vec2(-1.5, 0.5);
    fetchesOffsetsU *= texel.xx;
    fetchesOffsetsV *= texel.yy;

    const vec2 bilinearFetchOrigin = centerOfFetchesInTexelSpace * texel;
    uvs[0] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.x);
    uvs[1] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.x);
    uvs[2] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.y);
    uvs[3] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.y);

    weights[0] = fetchesWeightsU.x * fetchesWeightsV.x;
    weights[1] = fetchesWeightsU.y * fetchesWeightsV.x;
    weights[2] = fetchesWeightsU.x * fetchesWeightsV.y;
    weights[3] = fetchesWeightsU.y * fetchesWeightsV.y;
}

void hbgTentSamples5(vec2 coord, vec2 texel, out float weights[9], out vec2 uvs[9])
{
    const vec2 tentCenterInTexelSpace = coord / texel;
    const vec2 centerOfFetchesInTexelSpace = floor(tentCenterInTexelSpace + 0.5);
    const vec2 offsetFromTentCenter = tentCenterInTexelSpace - centerOfFetchesInTexelSpace;

    vec3 weightsU_A;
    vec3 weightsU_B;
    vec3 weightsV_A;
    vec3 weightsV_B;
    hbgTentWeights5(offsetFromTentCenter.x, weightsU_A, weightsU_B);
    hbgTentWeights5(offsetFromTentCenter.y, weightsV_A, weightsV_B);

    const vec3 fetchesWeightsU = vec3(weightsU_A.xz, weightsU_B.y) + vec3(weightsU_A.y, weightsU_B.xz);
    const vec3 fetchesWeightsV = vec3(weightsV_A.xz, weightsV_B.y) + vec3(weightsV_A.y, weightsV_B.xz);
    vec3 fetchesOffsetsU = vec3(weightsU_A.y, weightsU_B.xz) / fetchesWeightsU + vec3(-2.5, -0.5, 1.5);
    vec3 fetchesOffsetsV = vec3(weightsV_A.y, weightsV_B.xz) / fetchesWeightsV + vec3(-2.5, -0.5, 1.5);
    fetchesOffsetsU *= texel.xxx;
    fetchesOffsetsV *= texel.yyy;

    const vec2 bilinearFetchOrigin = centerOfFetchesInTexelSpace * texel;
    uvs[0] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.x);
    uvs[1] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.x);
    uvs[2] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.x);
    uvs[3] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.y);
    uvs[4] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.y);
    uvs[5] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.y);
    uvs[6] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.z);
    uvs[7] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.z);
    uvs[8] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.z);

    weights[0] = fetchesWeightsU.x * fetchesWeightsV.x;
    weights[1] = fetchesWeightsU.y * fetchesWeightsV.x;
    weights[2] = fetchesWeightsU.z * fetchesWeightsV.x;
    weights[3] = fetchesWeightsU.x * fetchesWeightsV.y;
    weights[4] = fetchesWeightsU.y * fetchesWeightsV.y;
    weights[5] = fetchesWeightsU.z * fetchesWeightsV.y;
    weights[6] = fetchesWeightsU.x * fetchesWeightsV.z;
    weights[7] = fetchesWeightsU.y * fetchesWeightsV.z;
    weights[8] = fetchesWeightsU.z * fetchesWeightsV.z;
}

void hbgTentSamples7(vec2 coord, vec2 texel, out float weights[16], out vec2 uvs[16])
{
    const vec2 tentCenterInTexelSpace = coord / texel;
    const vec2 centerOfFetchesInTexelSpace = floor(tentCenterInTexelSpace + 0.5);
    const vec2 offsetFromTentCenter = tentCenterInTexelSpace - centerOfFetchesInTexelSpace;

    vec4 weightsU_A;
    vec4 weightsU_B;
    vec4 weightsV_A;
    vec4 weightsV_B;
    hbgTentWeights7(offsetFromTentCenter.x, weightsU_A, weightsU_B);
    hbgTentWeights7(offsetFromTentCenter.y, weightsV_A, weightsV_B);

    const vec4 fetchesWeightsU = vec4(weightsU_A.xz, weightsU_B.xz) + vec4(weightsU_A.yw, weightsU_B.yw);
    const vec4 fetchesWeightsV = vec4(weightsV_A.xz, weightsV_B.xz) + vec4(weightsV_A.yw, weightsV_B.yw);
    vec4 fetchesOffsetsU = vec4(weightsU_A.yw, weightsU_B.yw) / fetchesWeightsU + vec4(-3.5, -1.5, 0.5, 2.5);
    vec4 fetchesOffsetsV = vec4(weightsV_A.yw, weightsV_B.yw) / fetchesWeightsV + vec4(-3.5, -1.5, 0.5, 2.5);
    fetchesOffsetsU *= texel.xxxx;
    fetchesOffsetsV *= texel.yyyy;

    const vec2 bilinearFetchOrigin = centerOfFetchesInTexelSpace * texel;
    uvs[0] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.x);
    uvs[1] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.x);
    uvs[2] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.x);
    uvs[3] = bilinearFetchOrigin + vec2(fetchesOffsetsU.w, fetchesOffsetsV.x);
    uvs[4] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.y);
    uvs[5] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.y);
    uvs[6] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.y);
    uvs[7] = bilinearFetchOrigin + vec2(fetchesOffsetsU.w, fetchesOffsetsV.y);
    uvs[8] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.z);
    uvs[9] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.z);
    uvs[10] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.z);
    uvs[11] = bilinearFetchOrigin + vec2(fetchesOffsetsU.w, fetchesOffsetsV.z);
    uvs[12] = bilinearFetchOrigin + vec2(fetchesOffsetsU.x, fetchesOffsetsV.w);
    uvs[13] = bilinearFetchOrigin + vec2(fetchesOffsetsU.y, fetchesOffsetsV.w);
    uvs[14] = bilinearFetchOrigin + vec2(fetchesOffsetsU.z, fetchesOffsetsV.w);
    uvs[15] = bilinearFetchOrigin + vec2(fetchesOffsetsU.w, fetchesOffsetsV.w);

    weights[0] = fetchesWeightsU.x * fetchesWeightsV.x;
    weights[1] = fetchesWeightsU.y * fetchesWeightsV.x;
    weights[2] = fetchesWeightsU.z * fetchesWeightsV.x;
    weights[3] = fetchesWeightsU.w * fetchesWeightsV.x;
    weights[4] = fetchesWeightsU.x * fetchesWeightsV.y;
    weights[5] = fetchesWeightsU.y * fetchesWeightsV.y;
    weights[6] = fetchesWeightsU.z * fetchesWeightsV.y;
    weights[7] = fetchesWeightsU.w * fetchesWeightsV.y;
    weights[8] = fetchesWeightsU.x * fetchesWeightsV.z;
    weights[9] = fetchesWeightsU.y * fetchesWeightsV.z;
    weights[10] = fetchesWeightsU.z * fetchesWeightsV.z;
    weights[11] = fetchesWeightsU.w * fetchesWeightsV.z;
    weights[12] = fetchesWeightsU.x * fetchesWeightsV.w;
    weights[13] = fetchesWeightsU.y * fetchesWeightsV.w;
    weights[14] = fetchesWeightsU.z * fetchesWeightsV.w;
    weights[15] = fetchesWeightsU.w * fetchesWeightsV.w;
}

float hbgSampleTent3(vec2 atlasUV, float depth, vec2 texel, vec2 uvMin, vec2 uvMax, bool punctual)
{
    float weights[4];
    vec2 uvs[4];
    hbgTentSamples3(atlasUV, texel, weights, uvs);
    float visibility = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        const vec2 uv = clamp(uvs[i], uvMin, uvMax);
        visibility += weights[i] * (punctual ? hbgShadowPunctualCompare(uv, depth)
                                             : hbgShadowMapCompare(uv, depth));
    }
    return visibility;
}

float hbgSampleTent5(vec2 atlasUV, float depth, vec2 texel, vec2 uvMin, vec2 uvMax, bool punctual)
{
    float weights[9];
    vec2 uvs[9];
    hbgTentSamples5(atlasUV, texel, weights, uvs);
    float visibility = 0.0;
    for (int i = 0; i < 9; ++i)
    {
        const vec2 uv = clamp(uvs[i], uvMin, uvMax);
        visibility += weights[i] * (punctual ? hbgShadowPunctualCompare(uv, depth)
                                             : hbgShadowMapCompare(uv, depth));
    }
    return visibility;
}

float hbgSampleTent7(vec2 atlasUV, float depth, vec2 texel, vec2 uvMin, vec2 uvMax, bool punctual)
{
    float weights[16];
    vec2 uvs[16];
    hbgTentSamples7(atlasUV, texel, weights, uvs);
    float visibility = 0.0;
    for (int i = 0; i < 16; ++i)
    {
        const vec2 uv = clamp(uvs[i], uvMin, uvMax);
        visibility += weights[i] * (punctual ? hbgShadowPunctualCompare(uv, depth)
                                             : hbgShadowMapCompare(uv, depth));
    }
    return visibility;
}

#if defined(HBG_SHADOW_POINT_SAMPLER) || defined(HBG_SHADOW_AREA_SAMPLER)
float hbgPcssNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

vec2 hbgPcssDiskSample(int index, int count, float radius, float jitter)
{
    const float sampleIndex = float(index) + 0.5;
    const float sampleRadius = radius * sqrt(sampleIndex / float(count));
    const float angle = sampleIndex * 2.39996323 + jitter;
    return vec2(cos(angle), sin(angle)) * sampleRadius;
}
#endif

#ifdef HBG_SHADOW_POINT_SAMPLER
float hbgPcssRawDepth(vec2 uv, bool punctual)
{
    if (punctual)
    {
        return texture2DLod(s_shadowMapPunctual, uv, 0.0).x;
    }
    return texture2DLod(s_shadowMap, uv, 0.0).x;
}

float hbgSamplePcss(vec2 atlasUV, float depth, vec2 texel, vec2 uvMin, vec2 uvMax, bool punctual,
                    float softness, float minFilterSize, int blockerSampleCount, int filterSampleCount,
                    float worldTexelSize, float nearPlane, float farPlane)
{
    const int blockers = clamp(blockerSampleCount, 1, 64);
    const int filters = clamp(filterSampleCount, 1, 64);
    const float jitter = hbgPcssNoise(atlasUV * 4096.0 +
                                      u_global_taaFrameInfo.z * 17.0 * u_global_taaFrameInfo.w) * 6.2831853;
    const float blockerRadius = clamp(softness * 32.0, 1.0, 16.0);
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    for (int blockerIndex = 0; blockerIndex < blockers; ++blockerIndex)
    {
        const vec2 offset = hbgPcssDiskSample(blockerIndex, blockers, blockerRadius, jitter) * texel;
        const vec2 uv = atlasUV + offset;
        if (all(greaterThanEqual(uv, uvMin)) && all(lessThanEqual(uv, uvMax)))
        {
            const float blockerDepth = hbgPcssRawDepth(uv, punctual);
            if (blockerDepth < depth)
            {
                blockerSum += blockerDepth;
                blockerCount += 1.0;
            }
        }
    }
    if (blockerCount < 1.0)
    {
        return 1.0;
    }

    const float averageBlockerDepth = blockerSum / blockerCount;
    float filterRadiusTexels = 1.0;
    const float minimumRadius = max(1.0, minFilterSize * 64.0);
    if (punctual)
    {
        const float nearZ = max(nearPlane, 1.0e-4);
        const float farZ = max(farPlane, nearZ + 1.0e-3);
        const float zScale = (farZ - nearZ) / (nearZ * farZ);
        const float zBias = 1.0 / farZ;
        const float receiverDistance = 1.0 / max(zScale * (1.0 - depth) + zBias, 1.0e-6);
        const float blockerDistance = 1.0 / max(zScale * (1.0 - averageBlockerDepth) + zBias, 1.0e-6);
        const float penumbra = abs(receiverDistance - blockerDistance) / max(blockerDistance, 1.0e-4);
        filterRadiusTexels = max(minimumRadius, penumbra * softness * 128.0);
    }
    else
    {
        const float depthRange = max(farPlane - nearPlane, 1.0e-3);
        const float receiverDistance = depth * depthRange;
        const float blockerDistance = averageBlockerDepth * depthRange;
        filterRadiusTexels = max(minimumRadius, abs(receiverDistance - blockerDistance) * softness /
                                                   max(worldTexelSize, 1.0e-5));
    }
    filterRadiusTexels = clamp(filterRadiusTexels, 1.0, 24.0);

    float visibility = 0.0;
    float validSamples = 0.0;
    for (int filterIndex = 0; filterIndex < filters; ++filterIndex)
    {
        const vec2 offset = hbgPcssDiskSample(filterIndex, filters, filterRadiusTexels, jitter) * texel;
        const vec2 uv = atlasUV + offset;
        if (all(greaterThanEqual(uv, uvMin)) && all(lessThanEqual(uv, uvMax)))
        {
            if (punctual)
            {
                visibility += hbgShadowPunctualCompare(uv, depth);
            }
            else
            {
                visibility += hbgShadowMapCompare(uv, depth);
            }
            validSamples += 1.0;
        }
    }
    return validSamples > 0.0 ? visibility / validSamples : 1.0;
}
#endif

float hbgSampleDirectionalRequest(int request, vec3 worldPosition, vec3 normalWS)
{
    const vec4 requestParams = hbgLoadShadowRequestParams(request);
    const vec4 requestInfo = hbgLoadShadowRequestInfo(request);
    const vec4 filterParams = hbgLoadShadowFilterParams(request);
    if (requestInfo.w < 0.5)
    {
        return 1.0;
    }

    const vec4 atlasRect = hbgLoadShadowAtlasRect(request);
    const mat4 shadowMatrix = transpose(hbgLoadShadowMatrix(request));
    const vec4 rawShadowCoord = mul(shadowMatrix, vec4(worldPosition, 1.0));
    if (rawShadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const vec3 biasedPosition =
        worldPosition + normalWS * requestParams.y * filterParams.z;
    const vec4 shadowCoord = mul(shadowMatrix, vec4(biasedPosition, 1.0));
    if (shadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const float inverseW = 1.0 / shadowCoord.w;
    const vec2 tileUV = shadowCoord.xy * inverseW;
    const float depth = shadowCoord.z * inverseW - requestParams.x;
    if (depth < 0.0 || depth > 1.0)
    {
        return 1.0;
    }

    const vec2 atlasUV = tileUV * atlasRect.xy + atlasRect.zw;
    if (any(lessThan(atlasUV, atlasRect.zw)) ||
        any(greaterThan(atlasUV, atlasRect.zw + atlasRect.xy)))
    {
        return 1.0;
    }

    const vec2 texel = atlasRect.xy * requestParams.zw;
    const vec2 uvMin = atlasRect.zw + texel * 0.5;
    const vec2 uvMax = atlasRect.zw + atlasRect.xy - texel * 0.5;
    const int filterMode = int(requestInfo.z + 0.5);
    if (filterMode == 8)
    {
#ifdef HBG_SHADOW_POINT_SAMPLER
        return hbgSampleIms(atlasUV, depth, texel, atlasRect, request);
#else
        return hbgSampleTent7(atlasUV, depth, texel, uvMin, uvMax, false);
#endif
    }
    if (filterMode == 7)
    {
#ifdef HBG_SHADOW_POINT_SAMPLER
        return hbgSampleMsm(atlasUV, depth, texel, uvMin, uvMax, 0);
#else
        return hbgSampleTent7(atlasUV, depth, texel, uvMin, uvMax, false);
#endif
    }
    if (filterMode == 6)
    {
#ifdef HBG_SHADOW_POINT_SAMPLER
        return hbgSampleVsm(atlasUV, depth, texel, uvMin, uvMax, 0);
#else
        return hbgSampleTent7(atlasUV, depth, texel, uvMin, uvMax, false);
#endif
    }
    if (filterMode == 5)
    {
#ifdef HBG_SHADOW_POINT_SAMPLER
        return hbgSampleEvsm(atlasUV, depth, texel, uvMin, uvMax, 0);
#else
        return hbgSampleTent7(atlasUV, depth, texel, uvMin, uvMax, false);
#endif
    }
    if (filterMode == 4)
    {
#ifdef HBG_SHADOW_POINT_SAMPLER
        const vec4 pcssParams = hbgLoadShadowPcssParams(request);
        return hbgSamplePcss(atlasUV, depth, texel, uvMin, uvMax, false, filterParams.x, pcssParams.z,
                             int(pcssParams.x + 0.5), int(pcssParams.y + 0.5), filterParams.z,
                             filterParams.y, pcssParams.w);
#else
        return hbgSampleTent7(atlasUV, depth, texel, uvMin, uvMax, false);
#endif
    }
    if (filterMode == 3)
    {
        return hbgSampleTent7(atlasUV, depth, texel, uvMin, uvMax, false);
    }
    if (filterMode == 2)
    {
        return hbgSampleTent5(atlasUV, depth, texel, uvMin, uvMax, false);
    }
    if (filterMode == 1)
    {
        return hbgSampleTent3(atlasUV, depth, texel, uvMin, uvMax, false);
    }
    return hbgShadowMapCompare(atlasUV, depth);
}

float hbgSamplePunctualRequest(int request, vec3 worldPosition, vec3 normalWS)
{
    const vec4 requestParams = hbgLoadShadowRequestParams(request);
    const vec4 requestInfo = hbgLoadShadowRequestInfo(request);
    const vec4 filterParams = hbgLoadShadowFilterParams(request);
    if (requestInfo.w < 0.5)
    {
        return 1.0;
    }

    const vec4 atlasRect = hbgLoadShadowAtlasRect(request);
    const mat4 shadowMatrix = transpose(hbgLoadShadowMatrix(request));
    const vec4 rawShadowCoord = mul(shadowMatrix, vec4(worldPosition, 1.0));
    if (rawShadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const bool punctual = true;
    const float distanceScale = max(rawShadowCoord.w, 1.0e-4);
    const vec3 biasedPosition =
        worldPosition + normalWS * requestParams.y * filterParams.z * distanceScale;
    const vec4 shadowCoord = mul(shadowMatrix, vec4(biasedPosition, 1.0));
    if (shadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const float inverseW = 1.0 / shadowCoord.w;
    const vec2 tileUV = shadowCoord.xy * inverseW;
    const float depth = shadowCoord.z * inverseW - requestParams.x;
    if (depth < 0.0 || depth > 1.0)
    {
        return 1.0;
    }

    const vec2 atlasUV = tileUV * atlasRect.xy + atlasRect.zw;
    if (any(lessThan(atlasUV, atlasRect.zw)) ||
        any(greaterThan(atlasUV, atlasRect.zw + atlasRect.xy)))
    {
        return 1.0;
    }

    const vec2 texel = atlasRect.xy * requestParams.zw;
    const vec2 uvMin = atlasRect.zw + texel * 0.5;
    const vec2 uvMax = atlasRect.zw + atlasRect.xy - texel * 0.5;
    const int filterMode = int(requestInfo.z + 0.5);
    if (filterMode == 7)
    {
        return hbgSampleMsm(atlasUV, depth, texel, uvMin, uvMax, 1);
    }
    if (filterMode == 6)
    {
        return hbgSampleVsm(atlasUV, depth, texel, uvMin, uvMax, 1);
    }
    if (filterMode == 5)
    {
        return hbgSampleEvsm(atlasUV, depth, texel, uvMin, uvMax, 1);
    }
    if (filterMode == 4)
    {
#ifdef HBG_SHADOW_POINT_SAMPLER
        const vec4 pcssParams = hbgLoadShadowPcssParams(request);
        return hbgSamplePcss(atlasUV, depth, texel, uvMin, uvMax, punctual, filterParams.x, pcssParams.z,
                             int(pcssParams.x + 0.5), int(pcssParams.y + 0.5), filterParams.z,
                             filterParams.y, pcssParams.w);
#else
        return hbgSampleTent5(atlasUV, depth, texel, uvMin, uvMax, punctual);
#endif
    }
    if (filterMode == 3)
    {
        return hbgSampleTent7(atlasUV, depth, texel, uvMin, uvMax, punctual);
    }
    if (filterMode == 2)
    {
        return hbgSampleTent5(atlasUV, depth, texel, uvMin, uvMax, punctual);
    }
    if (filterMode == 1)
    {
        return hbgSampleTent3(atlasUV, depth, texel, uvMin, uvMax, punctual);
    }
    return hbgShadowPunctualCompare(atlasUV, depth);
}

float hbgSampleAreaShadow(int request, vec3 worldPosition, vec3 normalWS)
{
#ifdef HBG_SHADOW_AREA_SAMPLER
    const vec4 requestParams = hbgLoadShadowRequestParams(request);
    const vec4 requestInfo = hbgLoadShadowRequestInfo(request);
    const vec4 filterParams = hbgLoadShadowFilterParams(request);
    if (requestInfo.w < 0.5)
    {
        return 1.0;
    }

    const vec4 atlasRect = hbgLoadShadowAtlasRect(request);
    const mat4 shadowMatrix = transpose(hbgLoadShadowMatrix(request));
    const vec4 rawShadowCoord = mul(shadowMatrix, vec4(worldPosition, 1.0));
    if (rawShadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const float distanceScale = max(rawShadowCoord.w, 1.0e-4);
    const vec3 biasedPosition =
        worldPosition + normalWS * requestParams.y * filterParams.z * distanceScale;
    const vec4 shadowCoord = mul(shadowMatrix, vec4(biasedPosition, 1.0));
    if (shadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const float inverseW = 1.0 / shadowCoord.w;
    const vec2 tileUV = shadowCoord.xy * inverseW;
    const float depth = shadowCoord.z * inverseW - requestParams.x;
    if (depth < 0.0 || depth > 1.0)
    {
        return 1.0;
    }

    const vec2 atlasUV = tileUV * atlasRect.xy + atlasRect.zw;
    if (any(lessThan(atlasUV, atlasRect.zw)) ||
        any(greaterThan(atlasUV, atlasRect.zw + atlasRect.xy)))
    {
        return 1.0;
    }

    const vec2 texel = atlasRect.xy * requestParams.zw;
    const vec2 uvMin = atlasRect.zw + texel * 0.5;
    const vec2 uvMax = atlasRect.zw + atlasRect.xy - texel * 0.5;
    const int filterMode = int(requestInfo.z + 0.5);
    if (filterMode == 7)
    {
        return hbgSampleMsm(atlasUV, depth, texel, uvMin, uvMax, 2);
    }
    if (filterMode == 6)
    {
        return hbgSampleVsm(atlasUV, depth, texel, uvMin, uvMax, 2);
    }
    if (filterMode == 5)
    {
        return hbgSampleEvsm(atlasUV, depth, texel, uvMin, uvMax, 2);
    }
    if (filterMode != 4)
    {
        float visibility = 0.0;
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                const vec2 uv =
                    clamp(atlasUV + vec2(float(x), float(y)) * texel * 1.5, uvMin, uvMax);
                visibility += texture2DLod(s_shadowMapPointPunctual, uv, 0.0).x >= depth ? 1.0 : 0.0;
            }
        }
        return visibility / 9.0;
    }

    const vec4 pcssParams = hbgLoadShadowPcssParams(request);
    const int blockers = clamp(int(pcssParams.x + 0.5), 1, 32);
    const int filters = clamp(int(pcssParams.y + 0.5), 1, 32);
    const float softness = max(filterParams.x, 1.0e-3);
    const float jitter = hbgPcssNoise(atlasUV * 4096.0 +
                                      u_global_taaFrameInfo.z * 17.0 * u_global_taaFrameInfo.w) * 6.2831853;
    const float blockerRadius = clamp(softness * 12.0, 1.0, 24.0);
    const float blockerZExtent = clamp(softness * 0.05, 1.0e-4, 0.5);
    float closestBlocker = 1.0;
    for (int blockerIndex = 0; blockerIndex < blockers; ++blockerIndex)
    {
        const float radial = sqrt((float(blockerIndex) + 0.5) / float(blockers));
        const float angle = float(blockerIndex) * 2.39996323 + jitter;
        const vec2 uv =
            atlasUV + vec2(cos(angle), sin(angle)) * radial * blockerRadius * texel;
        if (all(greaterThanEqual(uv, uvMin)) && all(lessThanEqual(uv, uvMax)))
        {
            const float rawDepth = texture2DLod(s_shadowMapPointPunctual, uv, 0.0).x;
            if (rawDepth < depth + blockerZExtent * radial)
            {
                closestBlocker = min(closestBlocker, rawDepth);
            }
        }
    }
    if (closestBlocker >= 1.0)
    {
        return 1.0;
    }

    const float nearZ = max(filterParams.y, 1.0e-4);
    const float farZ = max(pcssParams.w, nearZ + 1.0e-3);
    const float zScale = (farZ - nearZ) / (nearZ * farZ);
    const float zBias = 1.0 / farZ;
    const float receiverDistance = 1.0 / max(zScale * (1.0 - depth) + zBias, 1.0e-6);
    const float blockerDistance = 1.0 / max(zScale * (1.0 - closestBlocker) + zBias, 1.0e-6);
    const float penumbra = abs(receiverDistance - blockerDistance) / max(blockerDistance, 1.0e-4);
    const float minFilterRadius = max(1.0, pcssParams.z * 64.0);
    const float filterRadius = clamp(penumbra * softness * 96.0, minFilterRadius, 24.0);

    float visibility = 0.0;
    for (int filterIndex = 0; filterIndex < filters; ++filterIndex)
    {
        const float radial = sqrt((float(filterIndex) + 0.5) / float(filters));
        const float angle = float(filterIndex) * 2.39996323 + jitter * 1.37;
        const vec2 uv =
            atlasUV + vec2(cos(angle), sin(angle)) * radial * filterRadius * texel;
        if (all(greaterThanEqual(uv, uvMin)) && all(lessThanEqual(uv, uvMax)))
        {
            const float rawDepth = texture2DLod(s_shadowMapPointPunctual, uv, 0.0).x;
            visibility += rawDepth >= depth ? 1.0 : 0.0;
        }
        else
        {
            visibility += 1.0;
        }
    }
    return visibility / float(filters);
#else
    return 1.0;
#endif
}

int hbgShadowPointFace(vec3 direction)
{
    const vec3 magnitude = abs(direction);
    if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
    {
        return direction.x >= 0.0 ? 0 : 1;
    }
    if (magnitude.y >= magnitude.z)
    {
        return direction.y >= 0.0 ? 2 : 3;
    }
    return direction.z >= 0.0 ? 4 : 5;
}

float hbgSamplePunctualShadow(int requestBase, int lightType, vec3 lightPosition, vec3 lightDirection,
                              vec3 worldPosition, vec3 normalWS)
{
    if (requestBase < 0)
    {
        return 1.0;
    }
    int request = requestBase;
    if (lightType == 1)
    {
        request += hbgShadowPointFace(worldPosition - lightPosition);
    }
    else if (lightType != 2)
    {
        return 1.0;
    }
    return hbgSamplePunctualRequest(request, worldPosition, normalWS);
}

float hbgSampleDirectionalShadow(vec3 worldPosition, vec3 normalWS)
{
    if (u_pass_shadowParams.z < 0.5)
    {
        return 1.0;
    }

    const int cascadeCount = int(u_pass_shadowParams.w + 0.5);
    if (cascadeCount <= 0)
    {
        return 1.0;
    }

    const float viewDepth = -mul(u_global_view, vec4(worldPosition, 1.0)).z;
    const vec4 splits = u_pass_shadowCascadeSplits;
    int cascade = 0;
    if (viewDepth > splits.x)
    {
        cascade = 1;
    }
    if (viewDepth > splits.y)
    {
        cascade = 2;
    }
    if (viewDepth > splits.z)
    {
        cascade = 3;
    }
    const float lastSplit = cascadeCount == 1 ? splits.x : (cascadeCount == 2 ? splits.y : (cascadeCount == 3 ? splits.z : splits.w));
    if (cascade >= cascadeCount || viewDepth > lastSplit)
    {
        return 1.0;
    }

    float visibility = hbgSampleDirectionalRequest(cascade, worldPosition, normalWS);
    if (cascade == cascadeCount - 1)
    {
        const vec4 fade = hbgLoadShadowFade(cascade);
        const vec3 cameraDelta = worldPosition - u_global_cameraPos.xyz;
        const float alpha = hbgSaturate(dot(cameraDelta, cameraDelta) * fade.x + fade.y);
        visibility = mix(1.0, visibility, 1.0 - alpha);
    }
    return visibility;
}

float hbgSampleDirectionalRequestSingle(int request, vec3 worldPosition, vec3 normalWS)
{
    const vec4 requestParams = hbgLoadShadowRequestParams(request);
    const vec4 requestInfo = hbgLoadShadowRequestInfo(request);
    const vec4 filterParams = hbgLoadShadowFilterParams(request);
    if (requestInfo.w < 0.5)
    {
        return 1.0;
    }

    const vec4 atlasRect = hbgLoadShadowAtlasRect(request);
    const vec3 biasedPosition =
        worldPosition + normalWS * requestParams.y * filterParams.z;
    const vec4 shadowCoord = mul(transpose(hbgLoadShadowMatrix(request)), vec4(biasedPosition, 1.0));
    if (shadowCoord.w <= 0.0)
    {
        return 1.0;
    }

    const float inverseW = 1.0 / shadowCoord.w;
    const vec2 tileUV = shadowCoord.xy * inverseW;
    const float depth = shadowCoord.z * inverseW - requestParams.x;
    if (depth < 0.0 || depth > 1.0)
    {
        return 1.0;
    }

    const vec2 atlasUV = tileUV * atlasRect.xy + atlasRect.zw;
    if (any(lessThan(atlasUV, atlasRect.zw)) ||
        any(greaterThan(atlasUV, atlasRect.zw + atlasRect.xy)))
    {
        return 1.0;
    }
    return hbgShadowMapCompare(atlasUV, depth);
}

float hbgSampleDirectionalShadowVolumetric(vec3 worldPosition, vec3 normalWS)
{
    if (u_pass_shadowParams.z < 0.5)
    {
        return 1.0;
    }

    const int cascadeCount = int(u_pass_shadowParams.w + 0.5);
    if (cascadeCount <= 0)
    {
        return 1.0;
    }

    const float viewDepth = -mul(u_global_view, vec4(worldPosition, 1.0)).z;
    const vec4 splits = u_pass_shadowCascadeSplits;
    int cascade = 0;
    if (viewDepth > splits.x)
    {
        cascade = 1;
    }
    if (viewDepth > splits.y)
    {
        cascade = 2;
    }
    if (viewDepth > splits.z)
    {
        cascade = 3;
    }
    const float lastSplit = cascadeCount == 1 ? splits.x : (cascadeCount == 2 ? splits.y : (cascadeCount == 3 ? splits.z : splits.w));
    if (cascade >= cascadeCount || viewDepth > lastSplit)
    {
        return 1.0;
    }
    return hbgSampleDirectionalRequestSingle(cascade, worldPosition, normalWS);
}

#endif // HBG_SHADOWS_SC
