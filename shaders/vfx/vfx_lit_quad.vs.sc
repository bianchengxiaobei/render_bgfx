$output v_uv, v_color, v_worldPos, v_normal

#include "bgfx_shader.sh"

#define VFX_ATTRIBUTE_BUFFER_STAGE 12
#include "vfx/vfx_attributes.sc"

BUFFER_RAW_RO(s_vfxIndirection, 13);

uniform mat4 u_global_invView;
uniform mat4 u_global_viewProj;
uniform vec4 u_global_cameraPos;
uniform mat4 u_vfx_world;

void main()
{
    const uint vertexID = uint(gl_VertexID);
    const uint quadVertex = vertexID % 6u;
    const uint drawParticle = vertexID / 6u;
    const uint particle = rawLoadUint(s_vfxIndirection, drawParticle);
    VfxAttributes attributes;
    attributes.Init(particle);
    const vec3 position = attributes.Load_position();
    const float size = attributes.Load_size();
    const float angle = attributes.Load_angle();
    const vec3 scale = attributes.Load_scale();
    const vec4 color = vec4(attributes.Load_color(), attributes.Load_alpha());

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
    const float cosine = cos(angle);
    const float sine = sin(angle);
    const vec3 billboardRight = cameraRight * cosine + cameraUp * sine;
    const vec3 billboardUp = -cameraRight * sine + cameraUp * cosine;
    const vec2 offset = corner - 0.5;
    const vec3 localPosition = position + (billboardRight * (offset.x * scale.x) +
        billboardUp * (offset.y * scale.y)) * size;
    const vec3 worldPosition = mul(u_vfx_world, vec4(localPosition, 1.0)).xyz;

    gl_Position = mul(u_global_viewProj, vec4(worldPosition, 1.0));
    v_uv = corner;
    v_color = color;
    v_worldPos = worldPosition;
    v_normal = normalize(u_global_cameraPos.xyz - worldPosition);
}
