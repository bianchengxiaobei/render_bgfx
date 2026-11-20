#include "bgfx_compute.sh"
#include "vfx/vfx_spike_common.sc"

BUFFER_RW(s_vfxAuditCounters, uint, 0);
SHARED float s_vfxAuditShared[64];
uniform vec4 u_vfx_audit[2];

NUM_THREADS(64, 1, 1)

void main()
{
    const uint id = gl_GlobalInvocationID.x;
    s_vfxAuditShared[id] = u_vfx_audit[0].x + float(id);
    barrier();
    if (id == 0u)
    {
        uint original = 0u;
        atomicFetchAndAdd(s_vfxAuditCounters[0], 1u, original);
    }
}
