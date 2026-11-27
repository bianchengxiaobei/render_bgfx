#include "bgfx_compute.sh"
#include "postprocess/exposure_common.sc"

SAMPLER2D(s_luminance, 0);
IMAGE2D_WO(s_luminanceOut, rg16f, 1);

NUM_THREADS(8, 8, 1)

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(coord, ivec2(32, 32)))) {
        return;
    }

    float weightedEv = 0.0;
    float weightSum = 0.0;
    ivec2 base = coord * 8;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            vec2 sampleValue = texelFetch(s_luminance, base + ivec2(x, y), 0).rg;
            weightedEv += sampleValue.x * sampleValue.y;
            weightSum += sampleValue.y;
        }
    }

    float averageEv = weightSum > 0.0 ? weightedEv / weightSum : 0.0;
    imageStore(s_luminanceOut, coord, vec4(averageEv, weightSum, 0.0, 0.0));
}
