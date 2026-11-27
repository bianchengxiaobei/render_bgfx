#include "bgfx_compute.sh"
#include "water/water_fft_common.sc"

SAMPLER2DARRAY(s_waterFftSpectrum, 0);
UIMAGE2D_ARRAY_WO(s_waterFftOut, rgba16f, 1);

uniform vec4 u_wfTime;
uniform vec4 u_wfGridSizes;

NUM_THREADS(8, 8, 1)

void main()
{
    const ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (coord.x >= int(HBG_WATER_FFT_SIZE) || coord.y >= int(HBG_WATER_FFT_SIZE))
    {
        return;
    }

    const int i = coord.x < int(HBG_WATER_FFT_SIZE / 2u) ? coord.x : coord.x - int(HBG_WATER_FFT_SIZE);
    const int j = coord.y < int(HBG_WATER_FFT_SIZE / 2u) ? coord.y : coord.y - int(HBG_WATER_FFT_SIZE);
    const vec2 uv = (vec2(coord) + vec2(0.5, 0.5)) / float(HBG_WATER_FFT_SIZE);

    const vec2 k = vec2(float(i), float(j));
    const vec4 gridSizes = u_wfGridSizes;
    const vec2 k0 = k * (2.0 * HBG_WATER_PI / gridSizes.x);
    const vec2 k1 = k * (2.0 * HBG_WATER_PI / gridSizes.y);
    const vec2 k2 = k * (2.0 * HBG_WATER_PI / gridSizes.z);
    const vec2 k3 = k * (2.0 * HBG_WATER_PI / gridSizes.w);
    const vec4 spectrum01 = texture2DArrayLod(s_waterFftSpectrum, vec3(uv, 0.0), 0.0);
    const vec4 spectrum23 = texture2DArrayLod(s_waterFftSpectrum, vec3(uv, 1.0), 0.0);
    const vec2 h0 = hbgWaterSpectrum(k0, spectrum01.xy, u_wfTime.x);
    const vec2 h1 = hbgWaterSpectrum(k1, spectrum01.zw, u_wfTime.x);
    const vec2 h2 = hbgWaterSpectrum(k2, spectrum23.xy, u_wfTime.x);
    const vec2 h3 = hbgWaterSpectrum(k3, spectrum23.zw, u_wfTime.x);

    const vec2 slope0 = hbgWaterSlopeSpectrum(k0, h0);
    const vec2 slope1 = hbgWaterSlopeSpectrum(k1, h1);
    const vec2 slope2 = hbgWaterSlopeSpectrum(k2, h2);
    const vec2 slope3 = hbgWaterSlopeSpectrum(k3, h3);
    const vec2 choppy0 = hbgWaterChoppySpectrum(k0, h0);
    const vec2 choppy1 = hbgWaterChoppySpectrum(k1, h1);
    const vec2 choppy2 = hbgWaterChoppySpectrum(k2, h2);
    const vec2 choppy3 = hbgWaterChoppySpectrum(k3, h3);

    const vec2 packedHeight01 = vec2(h0.x - h1.y, h0.y + h1.x);
    const vec2 packedHeight23 = vec2(h2.x - h3.y, h2.y + h3.x);

    imageStore(s_waterFftOut, ivec3(coord, 0), vec4(packedHeight01, packedHeight23));
    imageStore(s_waterFftOut, ivec3(coord, 1), vec4(slope0, slope1));
    imageStore(s_waterFftOut, ivec3(coord, 2), vec4(slope2, slope3));
    imageStore(s_waterFftOut, ivec3(coord, 3), vec4(choppy0, choppy1));
    imageStore(s_waterFftOut, ivec3(coord, 4), vec4(choppy2, choppy3));
}
