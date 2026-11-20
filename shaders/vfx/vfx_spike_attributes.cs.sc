#include "bgfx_compute.sh"
#include "vfx/vfx_spike_common.sc"

BUFFER_RAW_RW(s_vfxAttributes, 0);
BUFFER_RAW_RW(s_vfxIndirection, 1);

NUM_THREADS(64, 1, 1)

void main()
{
    const uint particle = gl_GlobalInvocationID.x;
    const uint count = uint(u_vfx_params.x + 0.5);
    if (particle >= count || particle >= VFX_SPIKE_MAX_PARTICLES)
    {
        return;
    }

    const uint base = particle * VFX_SPIKE_STRIDE;
    const float column = float(particle & 31u) - 15.5;
    const float row = float((particle >> 5u) & 31u) - 15.5;
    const vec3 position = vec3(column * 0.08, row * 0.08, 0.0);
    const float size = 0.06;
    const float t = float(particle) / max(float(count - 1u), 1.0);
    const vec3 color = mix(vec3(0.08, 0.35, 1.0), vec3(1.0, 0.25, 0.05), t);

    rawStoreFloat(s_vfxAttributes, base + 0u, position.x);
    rawStoreFloat(s_vfxAttributes, base + 1u, position.y);
    rawStoreFloat(s_vfxAttributes, base + 2u, position.z);
    rawStoreFloat(s_vfxAttributes, base + 3u, size);
    rawStoreFloat(s_vfxAttributes, base + 4u, color.x);
    rawStoreFloat(s_vfxAttributes, base + 5u, color.y);
    rawStoreFloat(s_vfxAttributes, base + 6u, color.z);
    rawStoreFloat(s_vfxAttributes, base + 7u, 1.0);
    rawStoreFloat(s_vfxAttributes, base + 8u, 0.0);
    rawStoreFloat(s_vfxAttributes, base + 9u, 1.0);
    rawStoreFloat(s_vfxAttributes, base + 10u, 0.0);
    rawStoreFloat(s_vfxAttributes, base + 11u, 0.0);
    rawStoreFloat(s_vfxAttributes, base + 12u, 0.0);
    rawStoreFloat(s_vfxAttributes, base + 13u, 0.0);
    rawStoreFloat(s_vfxAttributes, base + 14u, 0.0);
    rawStoreFloat(s_vfxAttributes, base + 15u, 0.0);

    rawStoreUint(s_vfxIndirection, particle, particle);
}
