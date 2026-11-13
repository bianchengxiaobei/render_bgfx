#include "bgfx_compute.sh"
#include "common/common.sc"

BUFFER_RO(s_lightScreenBounds, vec4, 1);
BUFFER_RO(s_lightBigTileList, uint, 2);
BUFFER_WO(s_lightListOut, uint, 3);

#define HBG_LIGHT_TILE_SIZE 16.0
#define HBG_LIGHT_BIG_TILE_SIZE 64.0
#define HBG_LIGHT_LIST_STRIDE 33u
#define HBG_LIGHT_LIST_MAX_LIGHTS 32u

SHARED uint s_fptlMask;
SHARED uint s_fptlCount;
SHARED uint s_fptlList[32];

NUM_THREADS(64, 1, 1)

void main()
{
    const uint thread = gl_LocalInvocationIndex;
    const ivec2 tile = ivec2(gl_WorkGroupID.xy);
    const ivec2 bigTile = ivec2(tile.x >> 2, tile.y >> 2);
    if (thread == 0u)
    {
        s_fptlMask = 0u;
        s_fptlCount = 0u;
    }
    barrier();

    const vec2 screenSize = max(u_cluster_screenSize.xy, vec2(1.0, 1.0));
    const uint bigTileCountX = max((uint(screenSize.x) + 63u) / 64u, 1u);
    const uint fptlCountX = max((uint(screenSize.x) + 15u) / 16u, 1u);
    const uint bigTileBase = (uint(bigTile.y) * bigTileCountX + uint(bigTile.x)) * HBG_LIGHT_LIST_STRIDE;
    const uint bigTileLightCount = min(s_lightBigTileList[bigTileBase], HBG_LIGHT_LIST_MAX_LIGHTS);
    const vec2 tileMin = vec2(float(tile.x) * HBG_LIGHT_TILE_SIZE, float(tile.y) * HBG_LIGHT_TILE_SIZE);
    const vec2 tileMax = min(tileMin + vec2(HBG_LIGHT_TILE_SIZE, HBG_LIGHT_TILE_SIZE), screenSize);

    for (uint offset = thread; offset < bigTileLightCount; offset += 64u)
    {
        const uint lightIndex = s_lightBigTileList[bigTileBase + offset + 1u];
        if (lightIndex >= HBG_LIGHT_LIST_MAX_LIGHTS)
        {
            continue;
        }

        const vec4 minBounds = s_lightScreenBounds[lightIndex * 2u];
        const vec4 maxBounds = s_lightScreenBounds[lightIndex * 2u + 1u];
        if (maxBounds.z > 0.5 &&
            maxBounds.x > tileMin.x && minBounds.x < tileMax.x &&
            maxBounds.y > tileMin.y && minBounds.y < tileMax.y)
        {
            atomicOr(s_fptlMask, 1u << lightIndex);
        }
    }
    barrier();

    if (thread == 0u)
    {
        uint count = 0u;
        for (uint index = 0u; index < HBG_LIGHT_LIST_MAX_LIGHTS; ++index)
        {
            if ((s_fptlMask & (1u << index)) != 0u)
            {
                s_fptlList[count] = index;
                ++count;
            }
        }
        for (uint clearIndex = count; clearIndex < HBG_LIGHT_LIST_MAX_LIGHTS; ++clearIndex)
        {
            s_fptlList[clearIndex] = 0u;
        }
        s_fptlCount = count;
    }
    barrier();

    const uint output = (uint(tile.y) * fptlCountX + uint(tile.x)) * HBG_LIGHT_LIST_STRIDE;
    if (thread == 0u)
    {
        s_lightListOut[output] = s_fptlCount;
    }
    if (thread < HBG_LIGHT_LIST_MAX_LIGHTS)
    {
        s_lightListOut[output + thread + 1u] = s_fptlList[thread];
    }
}
