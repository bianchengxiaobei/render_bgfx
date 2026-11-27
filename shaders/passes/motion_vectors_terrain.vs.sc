$input a_position
$output v_currentClip, v_prevClip

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

    v_currentClip = mul(u_global_nonJitteredViewProj, worldPosition);
    v_prevClip = mul(u_global_prevViewProj, worldPosition);
    gl_Position = mul(u_global_viewProj, worldPosition);
}
