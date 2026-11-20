#include "bgfx_compute.sh"

UIMAGE2D_RW(s_shadowSat, rgba32ui, 0);

uniform vec4 u_shadowMomentSlot;

NUM_THREADS(64, 1, 1)

void main()
{
    const uint column = gl_WorkGroupID.x * 64u + gl_LocalInvocationID.x;
    const uint width = uint(u_shadowMomentSlot.x);
    const uint height = uint(u_shadowMomentSlot.y);
    if (column >= width || height == 0u)
    {
        return;
    }

    ivec2 origin = ivec2(u_shadowMomentSlot.zw);
    uvec4 sum = uvec4(0u, 0u, 0u, 0u);
    for (uint y = 0u; y < height; ++y)
    {
        const ivec2 pixel = origin + ivec2(int(column), int(y));
        sum += imageLoad(s_shadowSat, pixel);
        imageStore(s_shadowSat, pixel, sum);
    }
}
