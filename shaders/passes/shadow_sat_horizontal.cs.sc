#include "bgfx_compute.sh"

SAMPLER2D(s_shadowMoment, 0);
UIMAGE2D_WO(s_shadowSatOut, rgba32ui, 1);

uniform vec4 u_shadowMomentSlot;
uniform vec4 u_shadowMomentParams;

NUM_THREADS(64, 1, 1)

void main()
{
    const uint lineIndex = gl_WorkGroupID.x * 64u + gl_LocalInvocationID.x;
    const uint width = uint(u_shadowMomentSlot.x);
    const uint height = uint(u_shadowMomentSlot.y);
    if (lineIndex >= height || width == 0u)
    {
        return;
    }

    // HDRP MomentShadows.compute: fixed-precision horizontal prefix sum.
    const float kernelSize = max(u_shadowMomentParams.x, 1.0);
    const float maxKernelSize = max(ceil(1.0 + kernelSize), 1.0);
    const float fixedPrecision = 4294967295.0 / max(maxKernelSize * maxKernelSize, 1.0);

    ivec2 origin = ivec2(u_shadowMomentSlot.zw);
    uvec4 sum = uvec4(0u, 0u, 0u, 0u);
    for (uint x = 0u; x < width; ++x)
    {
        const ivec2 pixel = origin + ivec2(int(x), int(lineIndex));
        const vec4 moments = max(texelFetch(s_shadowMoment, pixel, 0), vec4(0.0, 0.0, 0.0, 0.0));
        sum += uvec4(moments * fixedPrecision);
        imageStore(s_shadowSatOut, pixel, sum);
    }
}
