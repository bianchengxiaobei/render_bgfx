#include "bgfx_compute.sh"
#include "postprocess/exposure_common.sc"

SAMPLER2D(s_source, 0);
IMAGE2D_WO(s_luminanceOut, rg16f, 1);

NUM_THREADS(8, 8, 1)

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(coord, ivec2(256, 256)))) {
        return;
    }

    vec2 renderSize = max(u_postprocess_screenParams.xy, vec2(1.0, 1.0));
    vec2 uv = (vec2(coord) + vec2(0.5, 0.5)) / 256.0;
    ivec2 pixel = ivec2(clamp(uv * renderSize, vec2(0.0, 0.0), renderSize - vec2(1.0, 1.0)));
    vec3 color = max(texelFetch(s_source, pixel, 0).rgb, vec3(0.0, 0.0, 0.0));
    float luminance = clamp(hbgLuminance(color), 1.0e-4, 1.0e4);
    float ev100 = hbgEv100FromLuminance(luminance);
    float weight = hbgMeteringWeight(vec2(coord) + vec2(0.5, 0.5), vec2(256.0, 256.0));
    imageStore(s_luminanceOut, coord, vec4(ev100, weight, 0.0, 0.0));
}
