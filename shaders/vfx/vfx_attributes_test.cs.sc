#define VFX_ATTRIBUTE_BUFFER_STAGE 0
#define VFX_ATTRIBUTE_BUFFER_WRITE 1

#include "vfx/vfx_attributes.sc"

BUFFER_RAW_RW(s_vfxIndirectArgs, 1);

NUM_THREADS(64, 1, 1)

void main()
{
    const uint index = gl_GlobalInvocationID.x;
    VfxAttributes attributes;
    attributes.Init(index);
    attributes.Store_position(vec3(float(index), 2.0, 3.0));
    attributes.Store_color(vec3(1.0, 0.5, 0.25));
    attributes.Store_alpha(1.0);
    attributes.Store_size(0.1);
    attributes.Store_age(0.0);
    attributes.Store_lifetime(1.0);
    attributes.Store_seed(index);
    attributes.Store_particleId(index);
    attributes.Store_spawnIndex(index);
    attributes.Store_alive(true);
    attributes.Store_angle(0.0);
    attributes.Store_scale(vec3(1.0, 1.0, 1.0));
    attributes.Store_targetPosition(vec3(0.0, 0.0, 0.0));
    attributes.Store_oldPosition(vec3(0.0, 0.0, 0.0));
    attributes.Store_user0(vec4(1.0, 2.0, 3.0, 4.0));

    const float value = attributes.Load_position().x +
                        attributes.Load_alpha() +
                        float(attributes.Load_seed()) +
                        (attributes.Load_alive() ? 1.0 : 0.0) +
                        attributes.Load_user0().w;
    if (index == 0u)
    {
        rawStoreUint(s_vfxIndirectArgs, 0u, uint(value));
    }
}
