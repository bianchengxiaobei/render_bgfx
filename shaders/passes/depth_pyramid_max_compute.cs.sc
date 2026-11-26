#include "bgfx_compute.sh"
#include "common/common.sc"

SAMPLER2D(s_depthSource, 0);
IMAGE2D_WO(s_depthPyramidOut, r32f, 1);

NUM_THREADS(8, 8, 1)

void main()
{
    const ivec2 dst = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 dstSize = imageSize(s_depthPyramidOut);
    if (any(greaterThanEqual(dst, dstSize)))
    {
        return;
    }

    const ivec2 sourceSize = max(ivec2(u_prepass_depthPyramidParams.zw + vec2(0.5, 0.5)), ivec2(1, 1));
    const bool copyDepth = all(equal(sourceSize, dstSize));
    float maxDepth = 1.0;
    if (copyDepth)
    {
        maxDepth = texelFetch(s_depthSource, dst, 0).r;
    }
    else
    {
        const ivec2 sourcePixel = dst * 2;
        const ivec2 sourceLimit = sourceSize - ivec2(1, 1);
        const float p00 = texelFetch(s_depthSource, clamp(sourcePixel + ivec2(0, 0), ivec2(0, 0), sourceLimit), 0).r;
        const float p10 = texelFetch(s_depthSource, clamp(sourcePixel + ivec2(1, 0), ivec2(0, 0), sourceLimit), 0).r;
        const float p01 = texelFetch(s_depthSource, clamp(sourcePixel + ivec2(0, 1), ivec2(0, 0), sourceLimit), 0).r;
        const float p11 = texelFetch(s_depthSource, clamp(sourcePixel + ivec2(1, 1), ivec2(0, 0), sourceLimit), 0).r;
        maxDepth = max(max(p00, p10), max(p01, p11));
    }
    const ivec2 storeCoord = copyDepth ? ivec2(dst.x, dstSize.y - 1 - dst.y) : dst;
    imageStore(s_depthPyramidOut, storeCoord, vec4(maxDepth, maxDepth, maxDepth, 1.0));
}
