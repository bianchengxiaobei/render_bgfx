#include "bgfx_compute.sh"

SAMPLER2D(s_shadowDepth, 0);
IMAGE2D_WO(s_shadowMoments, rgba32f, 1);

uniform vec4 u_shadowMomentSlot;

NUM_THREADS(8, 8, 1)

void main()
{
    ivec2 local = ivec2(gl_WorkGroupID.xy * 8u + gl_LocalInvocationID.xy);
    ivec2 size = ivec2(u_shadowMomentSlot.xy);
    if (any(greaterThanEqual(local, size)))
    {
        return;
    }

    ivec2 pixel = ivec2(u_shadowMomentSlot.zw) + local;
    const float depth = clamp(texelFetch(s_shadowDepth, pixel, 0).x, 0.0, 1.0);
    const float z = (1.0 - depth) * 2.0 - 1.0;
    const float z2 = z * z;
    const float z3 = z2 * z;
    const float z4 = z3 * z;

    // HDRP MomentShadows.compute: optimized four-moment basis.
    vec4 moments = vec4(z, z2, z3, z4);
    moments.x = 1.5 * z - 2.0 * z3 + 0.5;
    moments.z = sqrt(3.0) * 0.5 * z - sqrt(3.0) * (2.0 / 9.0) * z3 + 0.5;
    moments.y = 4.0 * z2 - 4.0 * z4;
    moments.w = 0.5 * z2 + 0.5 * z4;

    imageStore(s_shadowMoments, pixel, moments);
}
