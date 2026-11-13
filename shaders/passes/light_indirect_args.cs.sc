#include "bgfx_compute.sh"
#include "common/common.sc"

BUFFER_WO(s_lightIndirectArgs, uvec4, 1);

NUM_THREADS(1, 1, 1)

void main()
{
    const vec2 screenSize = max(u_cluster_screenSize.xy, vec2(1.0, 1.0));
    const uint groupsX = max((uint(screenSize.x) + 15u) / 16u, 1u);
    const uint groupsY = max((uint(screenSize.y) + 15u) / 16u, 1u);
    dispatchIndirect(s_lightIndirectArgs, 0, groupsX, groupsY, 1u);
}
