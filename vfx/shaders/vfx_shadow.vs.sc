$output v_uv

#include "bgfx_shader.sh"
#include "bgfx_compute.sh"

BUFFER_RAW_RO(s_vfxAttributes, 12);
BUFFER_RAW_RO(s_vfxIndirection, 13);
BUFFER_RAW_RO(s_vfxRenderCount, 14);
BUFFER_RAW_RO(s_vfxInstances, 15);

uniform mat4 u_global_invView;
uniform mat4 u_vfxShadowViewProj;
uniform vec4 u_vfxShadowParams;
uniform mat4 u_vfx_world;
uniform vec4 u_vfxInstanceParams;

void main()
{
    const uint vertexID = uint(gl_VertexID);
    const uint renderCount = rawLoadUint(s_vfxRenderCount, 0u);
    const uint particleChunks = max((renderCount + 2047u) / 2048u, 1u);
    const uint localInstance = uint(gl_InstanceID) / particleChunks;
    const uint particleChunk = uint(gl_InstanceID) - localInstance * particleChunks;
    const uint instanceIndex = localInstance + uint(u_vfxInstanceParams.z + 0.5);
    const uint linearParticle = vertexID / 6u + particleChunk * 2048u;
    gl_Position = vec4(0.0, 0.0, 0.0, 0.0);
    v_uv = vec2(0.0, 0.0);
    if (linearParticle < renderCount)
    {
    const uint quadVertex = vertexID % 6u;
    uint drawParticle = linearParticle;
    if (u_vfxShadowParams.w > 0.5)
    {
        drawParticle = renderCount - 1u - drawParticle;
    }
    const uint particle = rawLoadUint(s_vfxIndirection, drawParticle);
    const uint base = particle * uint(u_vfxShadowParams.z + 0.5);
    const vec3 position = vec3(rawLoadFloat(s_vfxAttributes, base + 0u),
        rawLoadFloat(s_vfxAttributes, base + 1u), rawLoadFloat(s_vfxAttributes, base + 2u));
    const float size = rawLoadFloat(s_vfxAttributes, base + 10u);
    const float angle = rawLoadFloat(s_vfxAttributes, base + 17u);
    const vec3 scale = vec3(rawLoadFloat(s_vfxAttributes, base + 18u),
        rawLoadFloat(s_vfxAttributes, base + 19u), rawLoadFloat(s_vfxAttributes, base + 20u));

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
    const vec3 billboardOffset = (billboardRight * (offset.x * scale.x) +
        billboardUp * (offset.y * scale.y)) * size;
    const vec3 localPosition = position + billboardOffset;
    vec3 worldPosition = mul(u_vfx_world, vec4(localPosition, 1.0)).xyz;
    if (u_vfxInstanceParams.x > 1.5)
    {
        const uint instanceBase = instanceIndex * 32u;
        const vec3 instanceX = vec3(rawLoadFloat(s_vfxInstances, instanceBase + 0u),
            rawLoadFloat(s_vfxInstances, instanceBase + 1u),
            rawLoadFloat(s_vfxInstances, instanceBase + 2u));
        const vec3 instanceY = vec3(rawLoadFloat(s_vfxInstances, instanceBase + 4u),
            rawLoadFloat(s_vfxInstances, instanceBase + 5u),
            rawLoadFloat(s_vfxInstances, instanceBase + 6u));
        const vec3 instanceZ = vec3(rawLoadFloat(s_vfxInstances, instanceBase + 8u),
            rawLoadFloat(s_vfxInstances, instanceBase + 9u),
            rawLoadFloat(s_vfxInstances, instanceBase + 10u));
        const vec3 instanceT = vec3(rawLoadFloat(s_vfxInstances, instanceBase + 12u),
            rawLoadFloat(s_vfxInstances, instanceBase + 13u),
            rawLoadFloat(s_vfxInstances, instanceBase + 14u));
        worldPosition = instanceX * localPosition.x + instanceY * localPosition.y +
            instanceZ * localPosition.z + instanceT;
    }

    v_uv = corner;
    gl_Position = mul(u_vfxShadowViewProj, vec4(worldPosition, 1.0));
    }
}
