#include "bgfx_compute.sh"
#include "water/water_fft_common.sc"

SAMPLER2DARRAY(s_waterFftInput, 0);
UIMAGE2D_ARRAY_WO(s_waterFftOutput, rgba16f, 1);

NUM_THREADS(8, 8, 1)

void main()
{
    const ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(HBG_WATER_FFT_SIZE) || coord.y >= int(HBG_WATER_FFT_SIZE))
    {
        return;
    }
    const uint layer = gl_WorkGroupID.z;
    if (layer >= HBG_WATER_FFT_LAYERS)
    {
        return;
    }
    const vec2 uv = (vec2(coord) + vec2(0.5, 0.5)) / float(HBG_WATER_FFT_SIZE);
    const vec4 value = texture2DArrayLod(s_waterFftInput, vec3(uv, float(layer)), 0.0);
    imageStore(s_waterFftOutput, ivec3(coord, int(layer)), value);
}
