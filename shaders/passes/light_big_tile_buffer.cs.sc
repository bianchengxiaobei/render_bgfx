#include "bgfx_compute.sh"
#include "common/common.sc"

BUFFER_RO(s_lightScreenBounds, vec4, 1);
BUFFER_WO(s_lightBigTileListOut, uint, 2);

#define HBG_LIGHT_BIG_TILE_SIZE 64.0
#define HBG_LIGHT_LIST_STRIDE 33u
#define HBG_LIGHT_LIST_MAX_LIGHTS 32u

SHARED uint s_bigTileMask;
SHARED uint s_bigTileCount;
SHARED uint s_bigTileList[32];

NUM_THREADS(64, 1, 1)

void main()
{
    const uint thread = gl_LocalInvocationIndex;
    const ivec2 bigTile = ivec2(gl_WorkGroupID.xy);
    if (thread == 0u)
    {
        s_bigTileMask = 0u;
        s_bigTileCount = 0u;
    }
    barrier();

    const uint lightCount = min(uint(max(u_cluster_lightCounts.x, 0.0) + 0.5), HBG_LIGHT_LIST_MAX_LIGHTS);
    const uint directionalCount = min(uint(max(u_cluster_lightCounts.y, 0.0) + 0.5), lightCount);
    const vec2 screenSize = max(u_cluster_screenSize.xy, vec2(1.0, 1.0));
    const uint bigTileCountX = max((uint(screenSize.x) + 63u) / 64u, 1u);
    const vec2 tileMin = vec2(float(bigTile.x) * HBG_LIGHT_BIG_TILE_SIZE,
                              float(bigTile.y) * HBG_LIGHT_BIG_TILE_SIZE);
    const vec2 tileMax = min(tileMin + vec2(HBG_LIGHT_BIG_TILE_SIZE, HBG_LIGHT_BIG_TILE_SIZE), screenSize);

    const uint lightIndex = thread;
    if (lightIndex >= directionalCount && lightIndex < lightCount)
    {
        const vec4 minBounds = s_lightScreenBounds[lightIndex * 2u];
        const vec4 maxBounds = s_lightScreenBounds[lightIndex * 2u + 1u];
        if (maxBounds.z > 0.5 &&
            maxBounds.x > tileMin.x && minBounds.x < tileMax.x &&
            maxBounds.y > tileMin.y && minBounds.y < tileMax.y)
        {
            atomicOr(s_bigTileMask, 1u << lightIndex);
        }
    }
    barrier();

    if (thread == 0u)
    {
        uint count = 0u;
        for (uint index = 0u; index < HBG_LIGHT_LIST_MAX_LIGHTS; ++index)
        {
            if ((s_bigTileMask & (1u << index)) != 0u)
            {
                s_bigTileList[count] = index;
                ++count;
            }
        }
        for (uint clearIndex = count; clearIndex < HBG_LIGHT_LIST_MAX_LIGHTS; ++clearIndex)
        {
            s_bigTileList[clearIndex] = 0u;
        }
        s_bigTileCount = count;
    }
    barrier();

    const uint output = (uint(bigTile.y) * bigTileCountX + uint(bigTile.x)) * HBG_LIGHT_LIST_STRIDE;
    if (thread == 0u)
    {
        s_lightBigTileListOut[output] = s_bigTileCount;
    }
    if (thread < HBG_LIGHT_LIST_MAX_LIGHTS)
    {
        s_lightBigTileListOut[output + thread + 1u] = s_bigTileList[thread];
    }
}
