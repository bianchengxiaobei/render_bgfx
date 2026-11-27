#include "bgfx_compute.sh"
#include "common/common.sc"

BUFFER_RO(s_lightScreenBounds, vec4, 1);
BUFFER_WO(s_lightBigTileListOut, uint, 2);
SAMPLER2D(s_lightData, 5);

#define HBG_LIGHT_BIG_TILE_SIZE 64.0
#define HBG_LIGHT_LIST_MAX_LIGHTS 128u
#define HBG_LIGHT_LIST_MASK_WORDS 4u
#define HBG_LIGHT_CATEGORY_COUNT 2u
#define HBG_LIGHT_FEATURE_DIRECTIONAL 4u
#define HBG_LIGHT_FEATURE_FLAGS_MASK 0xffffu

SHARED uint s_bigTileMask[HBG_LIGHT_LIST_MASK_WORDS];
SHARED uint s_bigTileCount[HBG_LIGHT_CATEGORY_COUNT];
SHARED uint s_bigTileList[HBG_LIGHT_CATEGORY_COUNT * HBG_LIGHT_LIST_MAX_LIGHTS];
SHARED uint s_bigTileFeatureFlags;

NUM_THREADS(64, 1, 1)

void main()
{
    const uint thread = gl_LocalInvocationIndex;
    const ivec2 bigTile = ivec2(gl_WorkGroupID.xy);
    if (thread == 0u)
    {
        for (uint category = 0u; category < HBG_LIGHT_CATEGORY_COUNT; ++category)
        {
            s_bigTileCount[category] = 0u;
        }
        for (uint word = 0u; word < HBG_LIGHT_LIST_MASK_WORDS; ++word)
        {
            s_bigTileMask[word] = 0u;
        }
    }
    for (uint clearIndex = thread; clearIndex < HBG_LIGHT_CATEGORY_COUNT * HBG_LIGHT_LIST_MAX_LIGHTS;
         clearIndex += 64u)
    {
        s_bigTileList[clearIndex] = 0u;
    }
    barrier();

    const uint capacity =
        min(uint(max(u_pass_lightListParams2.y, 0.0) + 0.5), HBG_LIGHT_LIST_MAX_LIGHTS);
    const uint listStride = max(uint(max(u_pass_lightListParams2.x, 0.0) + 0.5), 1u);
    const uint lightCount = min(uint(max(u_pass_lightCounts.x, 0.0) + 0.5), capacity);
    const uint directionalCount = min(uint(max(u_pass_lightCounts.y, 0.0) + 0.5), lightCount);
    const vec2 screenSize = max(u_pass_lightScreenSize.xy, vec2(1.0, 1.0));
    const uint bigTileCountX = max((uint(screenSize.x) + 63u) / 64u, 1u);
    const uint bigTileCountY = max((uint(screenSize.y) + 63u) / 64u, 1u);
    const vec2 tileMin = vec2(float(bigTile.x) * HBG_LIGHT_BIG_TILE_SIZE,
                              float(bigTile.y) * HBG_LIGHT_BIG_TILE_SIZE);
    const vec2 tileMax =
        min(tileMin + vec2(HBG_LIGHT_BIG_TILE_SIZE, HBG_LIGHT_BIG_TILE_SIZE), screenSize);

    for (uint lightIndex = thread; lightIndex < lightCount; lightIndex += 64u)
    {
        if (lightIndex < directionalCount)
        {
            continue;
        }

        const vec4 minBounds = s_lightScreenBounds[lightIndex * 2u];
        const vec4 maxBounds = s_lightScreenBounds[lightIndex * 2u + 1u];
        if (maxBounds.z > 0.5 &&
            maxBounds.x > tileMin.x && minBounds.x < tileMax.x &&
            maxBounds.y > tileMin.y && minBounds.y < tileMax.y)
        {
            atomicOr(s_bigTileMask[lightIndex >> 5u], 1u << (lightIndex & 31u));
        }
    }
    barrier();

    if (thread == 0u)
    {
        for (uint index = 0u; index < capacity; ++index)
        {
            if ((s_bigTileMask[index >> 5u] & (1u << (index & 31u))) == 0u)
            {
                continue;
            }

            const uint category = uint(max(s_lightScreenBounds[index * 2u + 1u].w + 0.5, 0.0));
            if (category >= HBG_LIGHT_CATEGORY_COUNT)
            {
                continue;
            }

            s_bigTileList[category * HBG_LIGHT_LIST_MAX_LIGHTS + s_bigTileCount[category]] = index;
            ++s_bigTileCount[category];
        }

        uint featureFlags = directionalCount > 0u ? HBG_LIGHT_FEATURE_DIRECTIONAL : 0u;
        for (uint category = 0u; category < HBG_LIGHT_CATEGORY_COUNT; ++category)
        {
            for (uint index = 0u; index < s_bigTileCount[category]; ++index)
            {
                const uint lightIndex = s_bigTileList[category * HBG_LIGHT_LIST_MAX_LIGHTS + index];
                featureFlags |= uint(texelFetch(s_lightData, ivec2(6, int(lightIndex)), 0).z + 0.5);
            }
        }
        s_bigTileFeatureFlags = featureFlags;
    }
    barrier();

    for (uint category = 0u; category < HBG_LIGHT_CATEGORY_COUNT; ++category)
    {
        const uint listBase = category * HBG_LIGHT_LIST_MAX_LIGHTS;
        const uint count = s_bigTileCount[category];
        const uint output =
            ((category * bigTileCountY + uint(bigTile.y)) * bigTileCountX + uint(bigTile.x)) * listStride;
        s_lightBigTileListOut[output] =
            (count & 0xffffu) | ((s_bigTileList[listBase] & 0xffffu) << 16u);
        for (uint wordIndex = thread; wordIndex < listStride; wordIndex += 64u)
        {
            if (wordIndex == 0u)
            {
                continue;
            }
            const uint lowIndex = wordIndex * 2u - 1u;
            const uint highIndex = lowIndex + 1u;
            const uint low =
                lowIndex < capacity ? (s_bigTileList[listBase + lowIndex] & 0xffffu) : 0u;
            const uint high =
                highIndex < capacity ? (s_bigTileList[listBase + highIndex] & 0xffffu) : 0u;
            s_lightBigTileListOut[output + wordIndex] = low | (high << 16u);
        }
    }
    barrier();

    if (thread == 0u)
    {
        const uint lastIndex = capacity - 1u;
        const uint lastLight = lastIndex < s_bigTileCount[0] ? s_bigTileList[lastIndex] : 0u;
        const uint featureWord =
            (lastLight & 0xffffu) | ((s_bigTileFeatureFlags & HBG_LIGHT_FEATURE_FLAGS_MASK) << 16u);
        const uint output =
            ((uint(bigTile.y) * bigTileCountX + uint(bigTile.x)) * listStride) + (listStride - 1u);
        s_lightBigTileListOut[output] = featureWord;
    }
}
