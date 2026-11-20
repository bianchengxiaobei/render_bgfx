#include "bgfx_compute.sh"
#include "vfx/vfx_spike_common.sc"

BUFFER_RAW_WO(s_vfxIndirectArgs, 0);

NUM_THREADS(1, 1, 1)

void main()
{
    const uint count = min(uint(u_vfx_params.x + 0.5), VFX_SPIKE_MAX_PARTICLES);
    rawStoreUint(s_vfxIndirectArgs, 0u, count * 6u);
    rawStoreUint(s_vfxIndirectArgs, 1u, 1u);
    rawStoreUint(s_vfxIndirectArgs, 2u, 0u);
    rawStoreUint(s_vfxIndirectArgs, 3u, 0u);
    rawStoreUint(s_vfxIndirectArgs, 4u, 0u);
    rawStoreUint(s_vfxIndirectArgs, 5u, 0u);
    rawStoreUint(s_vfxIndirectArgs, 6u, 0u);
    rawStoreUint(s_vfxIndirectArgs, 7u, 0u);
}
