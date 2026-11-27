#include "bgfx_compute.sh"
#include "water/water_fft_common.sc"

SAMPLER2DARRAY(s_waterFftInput, 0);
UIMAGE2D_ARRAY_WO(s_waterFftOutput, rgba16f, 1);

uniform vec4 u_wfMip;

NUM_THREADS(8, 8, 1)

void main()
{
    const int mip = int(u_wfMip.x + 0.5);
    const int size = max(int(HBG_WATER_FFT_SIZE) >> mip, 1);
    const ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= size || coord.y >= size)
    {
        return;
    }

    const uint layer = gl_WorkGroupID.z;
    const int step = 1 << mip;
    vec4 sum = vec4(0.0, 0.0, 0.0, 0.0);
    for (int y = 0; y < step; ++y)
    {
        for (int x = 0; x < step; ++x)
        {
            const vec2 source = vec2(coord * step + ivec2(x, y));
            const vec2 uv = (source + vec2(0.5, 0.5)) / float(HBG_WATER_FFT_SIZE);
            sum += texture2DArrayLod(s_waterFftInput, vec3(uv, float(layer)), 0.0);
        }
    }
    imageStore(s_waterFftOutput, ivec3(coord, int(layer)),
               sum * (1.0 / float(step * step)));
}
