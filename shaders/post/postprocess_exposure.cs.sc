#include "bgfx_compute.sh"
#include "common/common.sc"

IMAGE2D_RW(s_exposureOut, rgba16f, 0);

NUM_THREADS(1, 1, 1)

void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    float mode = u_postprocess_exposureParams.x;
    float exposure = mode < 0.5 ? max(u_postprocess_exposureParams.y, 0.0) : 1.0;
    imageStore(s_exposureOut, coord, vec4(exposure, 0.0, 0.0, 1.0));
}
