#include "bgfx_compute.sh"
#include "water/water_fft_common.sc"

#ifndef WATER_FFT_VERTICAL
#define WATER_FFT_VERTICAL 0
#endif

SAMPLER2DARRAY(s_waterFftInput, 0);
UIMAGE2D_ARRAY_WO(s_waterFftOutput, rgba16f, 1);

SHARED vec4 s_waterFftData[128];

NUM_THREADS(128, 1, 1)

void main()
{
    const uint thread = gl_LocalInvocationID.x;
    const uint layer = gl_WorkGroupID.z;
#if WATER_FFT_VERTICAL
    const uint rowOrColumn = gl_WorkGroupID.x;
    const ivec2 coord = ivec2(int(rowOrColumn), int(thread));
#else
    const uint rowOrColumn = gl_WorkGroupID.y;
    const ivec2 coord = ivec2(int(thread), int(rowOrColumn));
#endif
    const vec2 uv = (vec2(coord) + vec2(0.5, 0.5)) / float(HBG_WATER_FFT_SIZE);
    s_waterFftData[thread] = texture2DArrayLod(s_waterFftInput, vec3(uv, float(layer)), 0.0);
    barrier();

    uint reversed = 0u;
    for (uint bit = 0u; bit < 7u; ++bit)
    {
        reversed = (reversed << 1u) | ((thread >> bit) & 1u);
    }
    if (thread < reversed)
    {
        const vec4 tmp = s_waterFftData[thread];
        s_waterFftData[thread] = s_waterFftData[reversed];
        s_waterFftData[reversed] = tmp;
    }
    barrier();

    for (uint length = 2u; length <= HBG_WATER_FFT_SIZE; length <<= 1u)
    {
        const uint halfLength = length >> 1u;
        for (uint index = thread; index < halfLength; index += 128u)
        {
            const uint group = index / halfLength;
            const uint k = index - group * halfLength;
            const uint i0 = group * length + k;
            const uint i1 = i0 + halfLength;
            const float angle = 2.0 * HBG_WATER_PI * float(k) / float(length);
            const vec2 weight = vec2(cos(angle), sin(angle));
            const vec4 even = s_waterFftData[i0];
            const vec4 odd = vec4(hbgWaterComplexMul(s_waterFftData[i1].xy, weight),
                                  hbgWaterComplexMul(s_waterFftData[i1].zw, weight));
            s_waterFftData[i0] = even + odd;
            s_waterFftData[i1] = even - odd;
        }
        barrier();
    }

    imageStore(s_waterFftOutput, ivec3(coord, int(layer)), s_waterFftData[thread]);
}
