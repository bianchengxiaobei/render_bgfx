$output v_uv, v_color, v_worldPos, v_normal

#define VFX_ATTRIBUTE_BUFFER_STAGE 12

#include "vfx/vfx_spike_common.sc"
#include "vfx/vfx_attributes.sc"

void main()
{
    const uint particle = uint(gl_VertexID) / 6u;
    VfxAttributes attributes;
    attributes.Init(particle);

    const vec3 position = attributes.Load_position();
    gl_Position = mul(u_global_viewProj, vec4(position, 1.0));
    v_uv = vec2(0.0, 0.0);
    v_color = vec4(1.0, 1.0, 1.0, 1.0);
    v_worldPos = position;
    v_normal = vec3(0.0, 0.0, 1.0);
}
