#include "bgfx_compute.sh"

SAMPLER2D(s_shadowDepth, 0);
IMAGE2D_WO(s_evsmMoments, rgba32f, 1);

uniform vec4 u_shadowMomentSlot;
uniform vec4 u_shadowMomentParams;

#define HBG_EVSM_TILE 8
#define HBG_EVSM_HALO 4
#define HBG_EVSM_STRIDE 16
#define HBG_EVSM_CELLS 256

SHARED float s_moment1[HBG_EVSM_CELLS];
SHARED float s_moment2[HBG_EVSM_CELLS];

float hbgEvsmWeight(int offset)
{
    const int tap = abs(offset);
    if (tap == 0)
    {
        return 0.1531703;
    }
    if (tap == 1)
    {
        return 0.1448929;
    }
    if (tap == 2)
    {
        return 0.1226492;
    }
    if (tap == 3)
    {
        return 0.0929025;
    }
    return 0.06297021;
}

NUM_THREADS(8, 8, 1)

void main()
{
    const ivec2 sourceSize = ivec2(u_shadowMomentSlot.xy);
    const ivec2 sourceOrigin = ivec2(u_shadowMomentSlot.zw);
    const ivec2 halfSize = max((sourceSize + ivec2(1, 1)) >> 1, ivec2(1, 1));
    const ivec2 halfOrigin = sourceOrigin >> 1;
    const ivec2 groupOrigin = ivec2(gl_WorkGroupID.xy * uint(HBG_EVSM_TILE));
    const ivec2 local = ivec2(gl_LocalInvocationID.xy);
    const float exponent = u_shadowMomentParams.x;
    const ivec2 maxSource = sourceOrigin + sourceSize - ivec2(1, 1);

    for (uint cellIndex = gl_LocalInvocationIndex; cellIndex < uint(HBG_EVSM_CELLS);
         cellIndex += uint(HBG_EVSM_TILE * HBG_EVSM_TILE))
    {
        const int cellX = int(cellIndex % uint(HBG_EVSM_STRIDE));
        const int cellY = int(cellIndex / uint(HBG_EVSM_STRIDE));
        const ivec2 halfLocal =
            groupOrigin + ivec2(cellX - HBG_EVSM_HALO, cellY - HBG_EVSM_HALO);
        const ivec2 clampedHalf = clamp(halfLocal, ivec2(0, 0), halfSize - ivec2(1, 1));
        const ivec2 sourceBase = sourceOrigin + clampedHalf * 2;
        const ivec2 source0 = clamp(sourceBase, sourceOrigin, maxSource);
        const ivec2 source1 = clamp(sourceBase + ivec2(1, 1), sourceOrigin, maxSource);

        const float depth0 = clamp(texelFetch(s_shadowDepth, source0, 0).x, 0.0, 1.0);
        const float depth1 = clamp(texelFetch(s_shadowDepth, ivec2(source1.x, source0.y), 0).x, 0.0, 1.0);
        const float depth2 = clamp(texelFetch(s_shadowDepth, ivec2(source0.x, source1.y), 0).x, 0.0, 1.0);
        const float depth3 = clamp(texelFetch(s_shadowDepth, source1, 0).x, 0.0, 1.0);

        const float warped0 = exp2(exponent * (2.0 * depth0 - 1.0));
        const float warped1 = exp2(exponent * (2.0 * depth1 - 1.0));
        const float warped2 = exp2(exponent * (2.0 * depth2 - 1.0));
        const float warped3 = exp2(exponent * (2.0 * depth3 - 1.0));

        s_moment1[cellIndex] = (warped0 + warped1 + warped2 + warped3) * 0.25;
        s_moment2[cellIndex] =
            (warped0 * warped0 + warped1 * warped1 + warped2 * warped2 + warped3 * warped3) * 0.25;
    }
    barrier();

    const int tx = local.x;
    const int ty = local.y;
    float horizontal1a = 0.0;
    float horizontal2a = 0.0;
    float horizontal1b = 0.0;
    float horizontal2b = 0.0;
    for (int horizontalOffset = -HBG_EVSM_HALO; horizontalOffset <= HBG_EVSM_HALO; ++horizontalOffset)
    {
        const int column = tx + HBG_EVSM_HALO + horizontalOffset;
        const float weight = hbgEvsmWeight(horizontalOffset);
        horizontal1a += s_moment1[ty * HBG_EVSM_STRIDE + column] * weight;
        horizontal2a += s_moment2[ty * HBG_EVSM_STRIDE + column] * weight;
        horizontal1b += s_moment1[(ty + HBG_EVSM_TILE) * HBG_EVSM_STRIDE + column] * weight;
        horizontal2b += s_moment2[(ty + HBG_EVSM_TILE) * HBG_EVSM_STRIDE + column] * weight;
    }
    barrier();

    const int centerColumn = tx + HBG_EVSM_HALO;
    s_moment1[ty * HBG_EVSM_STRIDE + centerColumn] = horizontal1a;
    s_moment2[ty * HBG_EVSM_STRIDE + centerColumn] = horizontal2a;
    s_moment1[(ty + HBG_EVSM_TILE) * HBG_EVSM_STRIDE + centerColumn] = horizontal1b;
    s_moment2[(ty + HBG_EVSM_TILE) * HBG_EVSM_STRIDE + centerColumn] = horizontal2b;
    barrier();

    const int centerRow = ty + HBG_EVSM_HALO;
    float vertical1 = 0.0;
    float vertical2 = 0.0;
    for (int verticalOffset = -HBG_EVSM_HALO; verticalOffset <= HBG_EVSM_HALO; ++verticalOffset)
    {
        const int row = centerRow + verticalOffset;
        const float weight = hbgEvsmWeight(verticalOffset);
        vertical1 += s_moment1[row * HBG_EVSM_STRIDE + centerColumn] * weight;
        vertical2 += s_moment2[row * HBG_EVSM_STRIDE + centerColumn] * weight;
    }

    const ivec2 halfLocal = groupOrigin + local;
    if (all(lessThan(halfLocal, halfSize)))
    {
        imageStore(s_evsmMoments, halfOrigin + halfLocal, vec4(vertical1, vertical2, 0.0, 0.0));
    }
}
