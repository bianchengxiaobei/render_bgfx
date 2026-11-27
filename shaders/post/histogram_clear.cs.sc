#include "bgfx_compute.sh"

UIMAGE2D_WO(s_histogram, r32ui, 1);

NUM_THREADS(128, 1, 1)

void main()
{
    imageStore(s_histogram, ivec2(int(gl_GlobalInvocationID.x), 0), uvec4(0u, 0u, 0u, 0u));
}
