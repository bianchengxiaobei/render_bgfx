#include "bgfx_compute.sh"
#include "common/common.sc"

SAMPLER2D(s_lightScreenBounds, 1);
SAMPLER2D(s_lightData, 5);
UIMAGE2D_WO(s_lightBigTileListOut, r32ui, 2);

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

        const vec4 minBounds = texelFetch(s_lightScreenBounds, ivec2(int(lightIndex * 2u), 0), 0);
        const vec4 maxBounds = texelFetch(s_lightScreenBounds, ivec2(int(lightIndex * 2u + 1u), 0), 0);
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

            const uint category =
                uint(max(texelFetch(s_lightScreenBounds, ivec2(int(index * 2u + 1u), 0), 0).w + 0.5, 0.0));
            if (category >= HBG_LIGHT_CATEGORY_COUNT)
            {
                continue;
            }

            const uint listIndex = category * HBG_LIGHT_LIST_MAX_LIGHTS + s_bigTileCount[category];
            s_bigTileList[listIndex] = index;
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
        const ivec2 output =
            ivec2(bigTile.x * int(listStride), int(category * bigTileCountY) + bigTile.y);
        if (thread == 0u)
        {
            imageStore(s_lightBigTileListOut, output,
                       uvec4((count & 0xffffu) | ((s_bigTileList[listBase] & 0xffffu) << 16u), 0u, 0u, 0u));
        }
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
            imageStore(s_lightBigTileListOut, output + ivec2(int(wordIndex), 0),
                       uvec4(low | (high << 16u), 0u, 0u, 0u));
        }
    }
    barrier();

    if (thread == 0u)
    {
        const uint lastIndex = capacity - 1u;
        const uint lastLight = lastIndex < s_bigTileCount[0] ? s_bigTileList[lastIndex] : 0u;
        const uint featureWord =
            (lastLight & 0xffffu) | ((s_bigTileFeatureFlags & HBG_LIGHT_FEATURE_FLAGS_MASK) << 16u);
        const ivec2 output = ivec2(bigTile.x * int(listStride) + int(listStride - 1u), bigTile.y);
        imageStore(s_lightBigTileListOut, output, uvec4(featureWord, 0u, 0u, 0u));
    }
}
