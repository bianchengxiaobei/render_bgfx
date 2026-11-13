#include "bgfx_compute.sh"
#include "common/common.sc"

SAMPLER2D(s_lightScreenBounds, 1);
USAMPLER2D(s_lightBigTileList, 2);
UIMAGE2D_WO(s_lightListOut, r32ui, 3);

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

    const uint bigTileLightCount =
        min(texelFetch(s_lightBigTileList, ivec2(bigTile.x * int(HBG_LIGHT_LIST_STRIDE), bigTile.y), 0).x,
            HBG_LIGHT_LIST_MAX_LIGHTS);
    const vec2 screenSize = max(u_cluster_screenSize.xy, vec2(1.0, 1.0));
    const vec2 tileMin = vec2(float(tile.x) * HBG_LIGHT_TILE_SIZE, float(tile.y) * HBG_LIGHT_TILE_SIZE);
    const vec2 tileMax = min(tileMin + vec2(HBG_LIGHT_TILE_SIZE, HBG_LIGHT_TILE_SIZE), screenSize);

    for (uint offset = thread; offset < bigTileLightCount; offset += 64u)
    {
        const uint lightIndex =
            texelFetch(s_lightBigTileList, ivec2(bigTile.x * int(HBG_LIGHT_LIST_STRIDE) + int(offset) + 1,
                                                bigTile.y),
                       0)
                .x;
        if (lightIndex >= HBG_LIGHT_LIST_MAX_LIGHTS)
        {
            continue;
        }

        const vec4 minBounds = texelFetch(s_lightScreenBounds, ivec2(int(lightIndex * 2u), 0), 0);
        const vec4 maxBounds = texelFetch(s_lightScreenBounds, ivec2(int(lightIndex * 2u + 1u), 0), 0);
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


    const ivec2 output = ivec2(tile.x * int(HBG_LIGHT_LIST_STRIDE), tile.y);
    if (thread == 0u)
    {
        imageStore(s_lightListOut, output, uvec4(s_fptlCount, 0u, 0u, 0u));
    }
    if (thread < HBG_LIGHT_LIST_MAX_LIGHTS)
    {
        imageStore(s_lightListOut, output + ivec2(int(thread) + 1, 0),
                   uvec4(s_fptlList[thread], 0u, 0u, 0u));
    }
}
