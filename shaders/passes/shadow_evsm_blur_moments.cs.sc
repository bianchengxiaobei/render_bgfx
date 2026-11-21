#include "bgfx_compute.sh"

SAMPLER2D(s_shadowMoment, 0);
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
    const ivec2 halfSize = max(ivec2(u_shadowMomentSlot.xy), ivec2(1, 1));
    const ivec2 halfOrigin = ivec2(u_shadowMomentSlot.zw);
    const ivec2 groupOrigin = ivec2(gl_WorkGroupID.xy * uint(HBG_EVSM_TILE));
    const ivec2 local = ivec2(gl_LocalInvocationID.xy);
    const ivec2 halfLocal = groupOrigin + local;
    const ivec2 maxHalf = halfSize - ivec2(1, 1);

    if (u_shadowMomentParams.x > 0.5)
    {
        if (all(lessThan(halfLocal, halfSize)))
        {
            const ivec2 coord = halfOrigin + halfLocal;
            const vec4 moment = texelFetch(s_shadowMoment, coord, 0);
            imageStore(s_evsmMoments, coord, vec4(moment.xy, 0.0, 0.0));
        }
        return;
    }

    for (uint cellIndex = gl_LocalInvocationIndex; cellIndex < uint(HBG_EVSM_CELLS);
         cellIndex += uint(HBG_EVSM_TILE * HBG_EVSM_TILE))
    {
        const int cellX = int(cellIndex % uint(HBG_EVSM_STRIDE));
        const int cellY = int(cellIndex / uint(HBG_EVSM_STRIDE));
        const ivec2 cellLocal =
            groupOrigin + ivec2(cellX - HBG_EVSM_HALO, cellY - HBG_EVSM_HALO);
        const ivec2 source = halfOrigin + clamp(cellLocal, ivec2(0, 0), maxHalf);
        const vec4 moment = texelFetch(s_shadowMoment, source, 0);
        s_moment1[cellIndex] = moment.x;
        s_moment2[cellIndex] = moment.y;
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

    if (all(lessThan(halfLocal, halfSize)))
    {
        imageStore(s_evsmMoments, halfOrigin + halfLocal, vec4(vertical1, vertical2, 0.0, 0.0));
    }
}
