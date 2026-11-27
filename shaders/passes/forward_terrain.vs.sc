$input a_position, a_normal, a_tangent, a_texcoord0
$output v_texcoord0, v_worldPos, v_normal, v_tangent, v_bitangent

#include "common/common.sc"

SAMPLER2D(s_terrainHeight, 15);

float hbgTerrainHeight(vec2 worldXZ)
{
    const vec2 gridUv = (worldXZ - u_object_terrainWorld.xy) / u_object_terrainWorld.z;
    const float texels = u_object_terrainWorld.w;
    const float size = 1.0 / texels + 1.0;
    const vec2 sampleUv = (gridUv / texels + vec2(0.5, 0.5)) / size;
    return texture2DLod(s_terrainHeight, sampleUv, 0.0).x;
}

vec3 hbgTerrainNormal(vec2 worldXZ)
{
    const float worldStep = u_object_terrainWorld.z * u_object_terrainWorld.w;
    const float left = hbgTerrainHeight(worldXZ - vec2(worldStep, 0.0));
    const float right = hbgTerrainHeight(worldXZ + vec2(worldStep, 0.0));
    const float down = hbgTerrainHeight(worldXZ - vec2(0.0, worldStep));
    const float up = hbgTerrainHeight(worldXZ + vec2(0.0, worldStep));
    return normalize(vec3(-(right - left) / (2.0 * worldStep), 1.0,
                          -(up - down) / (2.0 * worldStep)));
}

void main()
{
    const vec2 gridUv = a_position.xz;
    const vec2 worldXZ = u_object_terrainNode.xy + gridUv * u_object_terrainNode.z;
    float height = hbgTerrainHeight(worldXZ);
    if (a_position.y > 0.5)
    {
        height -= u_object_terrainNode.w;
    }

    const vec4 worldPosition = vec4(worldXZ.x, height, worldXZ.y, 1.0);
    const vec3 worldNormal = hbgTerrainNormal(worldXZ);
    vec3 tangentWorld = mul(u_model[0], vec4(a_tangent.xyz, 0.0)).xyz;
    const float tangentLengthSq = dot(tangentWorld, tangentWorld);
    const bool hasTangent = tangentLengthSq > 1.0e-12;
    const vec3 worldTangent = hasTangent ? tangentWorld * inversesqrt(tangentLengthSq) : vec3(0.0, 0.0, 0.0);
    const vec3 worldBitangent = hasTangent ? cross(worldNormal, worldTangent) * a_tangent.w : vec3(0.0, 0.0, 0.0);

    gl_Position = mul(u_global_viewProj, worldPosition);

    v_worldPos = worldPosition.xyz;
    v_normal = worldNormal;
    v_tangent = worldTangent;
    v_bitangent = worldBitangent;
    v_texcoord0 = (worldXZ - u_object_terrainWorld.xy) / u_object_terrainWorld.z;
}
