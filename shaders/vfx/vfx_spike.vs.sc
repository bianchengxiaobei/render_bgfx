$output v_uv, v_color, v_worldPos, v_normal

#include "bgfx_compute.sh"
#include "vfx/vfx_spike_common.sc"

BUFFER_RAW_RO(s_vfxAttributes, 12);
BUFFER_RAW_RO(s_vfxIndirection, 13);

void main()
{
    const uint vertexID = uint(gl_VertexID);
    const uint quadVertex = vertexID % 6u;
    const uint drawParticle = vertexID / 6u;
    const uint particle = rawLoadUint(s_vfxIndirection, drawParticle);
    const uint base = particle * VFX_SPIKE_STRIDE;

    const vec3 position = vec3(rawLoadFloat(s_vfxAttributes, base + 0u),
                               rawLoadFloat(s_vfxAttributes, base + 1u),
                               rawLoadFloat(s_vfxAttributes, base + 2u));
    const float size = rawLoadFloat(s_vfxAttributes, base + 3u);
    const vec4 color = vec4(rawLoadFloat(s_vfxAttributes, base + 4u),
                            rawLoadFloat(s_vfxAttributes, base + 5u),
                            rawLoadFloat(s_vfxAttributes, base + 6u),
                            rawLoadFloat(s_vfxAttributes, base + 7u));

    vec2 corner;
    if (quadVertex == 0u)
    {
        corner = vec2(0.0, 0.0);
    }
    else if (quadVertex == 1u)
    {
        corner = vec2(1.0, 0.0);
    }
    else if (quadVertex == 2u)
    {
        corner = vec2(1.0, 1.0);
    }
    else if (quadVertex == 3u)
    {
        corner = vec2(0.0, 0.0);
    }
    else if (quadVertex == 4u)
    {
        corner = vec2(1.0, 1.0);
    }
    else
    {
        corner = vec2(0.0, 1.0);
    }

    const vec3 cameraRight = u_global_invView[0].xyz;
    const vec3 cameraUp = u_global_invView[1].xyz;
    const vec3 worldPosition = position + (cameraRight * (corner.x - 0.5) + cameraUp * (corner.y - 0.5)) * size;

    gl_Position = mul(u_global_viewProj, vec4(worldPosition, 1.0));
    v_uv = corner;
    v_color = color;
    v_worldPos = worldPosition;
    v_normal = normalize(u_global_cameraPos.xyz - worldPosition);
}
