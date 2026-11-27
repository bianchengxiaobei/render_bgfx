#include "bgfx_compute.sh"
#include "postprocess/exposure_common.sc"

SAMPLER2D(s_source, 0);
UIMAGE2D_RW(s_histogram, r32ui, 1);

NUM_THREADS(16, 8, 1)

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 pixel = coord * 2;
    ivec2 screenSize = ivec2(u_postprocess_screenParams.xy);
    if (any(greaterThanEqual(pixel, screenSize))) {
        return;
    }

    vec3 color = max(texelFetch(s_source, pixel, 0).rgb, vec3(0.0, 0.0, 0.0));
    float luminance = clamp(hbgLuminance(color), 1.0e-4, 1.0e4);
    float ev100 = hbgEv100FromLuminance(luminance);
    float fraction = hbgSaturate(ev100 * u_postprocess_histogramParams.x + u_postprocess_histogramParams.y);
    uint bin = uint(fraction * 127.0);
    float weight = hbgMeteringWeight(vec2(pixel) + vec2(0.5, 0.5), vec2(screenSize));
    uint packedWeight = uint(weight * 2048.0 + 0.5);
    imageAtomicAdd(s_histogram, ivec2(int(bin), 0), uvec4(packedWeight, 0u, 0u, 0u));
}
