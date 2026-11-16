#include "bgfx_compute.sh"
#include "common/common.sc"

BUFFER_RO(s_lightScreenBounds, vec4, 1);
BUFFER_RO(s_lightBigTileList, uint, 2);
BUFFER_WO(s_lightListOut, uint, 3);
SAMPLER2D(s_lightData, 5);

#define HBG_LIGHT_TILE_SIZE 16.0
#define HBG_LIGHT_BIG_TILE_SIZE 64.0
#define HBG_LIGHT_LIST_MAX_LIGHTS 128u
#define HBG_LIGHT_LIST_MASK_WORDS 4u
#define HBG_LIGHT_CATEGORY_COUNT 2u
#define HBG_LIGHT_FEATURE_DIRECTIONAL 4u
#define HBG_LIGHT_FEATURE_FLAGS_MASK 0xffffu

SHARED uint s_fptlMask[HBG_LIGHT_CATEGORY_COUNT * HBG_LIGHT_LIST_MASK_WORDS];
SHARED uint s_fptlCount[HBG_LIGHT_CATEGORY_COUNT];
SHARED uint s_fptlList[HBG_LIGHT_CATEGORY_COUNT * HBG_LIGHT_LIST_MAX_LIGHTS];

NUM_THREADS(64, 1, 1)

void main()
{
    const uint thread = gl_LocalInvocationIndex;
    const ivec2 tile = ivec2(gl_WorkGroupID.xy);
    const ivec2 bigTile = ivec2(tile.x >> 2, tile.y >> 2);
    if (thread == 0u)
    {
        for (uint category = 0u; category < HBG_LIGHT_CATEGORY_COUNT; ++category)
        {
            s_fptlCount[category] = 0u;
        }
        for (uint word = 0u; word < HBG_LIGHT_CATEGORY_COUNT * HBG_LIGHT_LIST_MASK_WORDS; ++word)
        {
            s_fptlMask[word] = 0u;
        }
    }
    for (uint clearIndex = thread; clearIndex < HBG_LIGHT_CATEGORY_COUNT * HBG_LIGHT_LIST_MAX_LIGHTS;
         clearIndex += 64u)
    {
        s_fptlList[clearIndex] = 0u;
    }
    barrier();

    const uint capacity =
        min(uint(max(u_pass_lightListParams2.y, 0.0) + 0.5), HBG_LIGHT_LIST_MAX_LIGHTS);
    const uint listStride = max(uint(max(u_pass_lightListParams2.x, 0.0) + 0.5), 1u);
    const uint directionalCount = min(uint(max(u_pass_lightCounts.y, 0.0) + 0.5), capacity);
    const vec2 screenSize = max(u_pass_lightScreenSize.xy, vec2(1.0, 1.0));
    const uint bigTileCountX = max((uint(screenSize.x) + 63u) / 64u, 1u);
    const uint bigTileCountY = max((uint(screenSize.y) + 63u) / 64u, 1u);
    const uint fptlCountX = max((uint(screenSize.x) + 15u) / 16u, 1u);
    const uint fptlCountY = max((uint(screenSize.y) + 15u) / 16u, 1u);
    const vec2 tileMin = vec2(float(tile.x) * HBG_LIGHT_TILE_SIZE,
                              float(tile.y) * HBG_LIGHT_TILE_SIZE);
    const vec2 tileMax =
        min(tileMin + vec2(HBG_LIGHT_TILE_SIZE, HBG_LIGHT_TILE_SIZE), screenSize);

    for (uint category = 0u; category < HBG_LIGHT_CATEGORY_COUNT; ++category)
    {
        const uint bigTileBase =
            ((category * bigTileCountY + uint(bigTile.y)) * bigTileCountX + uint(bigTile.x)) * listStride;
        const uint bigTileLightCount = min(s_lightBigTileList[bigTileBase] & 0xffffu, capacity);
        const uint maskBase = category * HBG_LIGHT_LIST_MASK_WORDS;
        for (uint offset = thread; offset < bigTileLightCount; offset += 64u)
        {
            const uint halfword = offset + 1u;
            const uint lightIndex =
                (s_lightBigTileList[bigTileBase + (halfword >> 1u)] >> ((halfword & 1u) * 16u)) & 0xffffu;
            if (lightIndex >= capacity)
            {
                continue;
            }

            const vec4 minBounds = s_lightScreenBounds[lightIndex * 2u];
            const vec4 maxBounds = s_lightScreenBounds[lightIndex * 2u + 1u];
            if (maxBounds.z > 0.5 &&
                maxBounds.x > tileMin.x && minBounds.x < tileMax.x &&
                maxBounds.y > tileMin.y && minBounds.y < tileMax.y)
            {
                atomicOr(s_fptlMask[maskBase + (lightIndex >> 5u)], 1u << (lightIndex & 31u));
            }
        }
    }
    barrier();

    if (thread == 0u)
    {
        for (uint category = 0u; category < HBG_LIGHT_CATEGORY_COUNT; ++category)
        {
            const uint maskBase = category * HBG_LIGHT_LIST_MASK_WORDS;
            const uint listBase = category * HBG_LIGHT_LIST_MAX_LIGHTS;
            uint count = 0u;
            for (uint index = 0u; index < capacity; ++index)
            {
                if ((s_fptlMask[maskBase + (index >> 5u)] & (1u << (index & 31u))) != 0u)
                {
                    s_fptlList[listBase + count] = index;
                    ++count;
                }
            }
            s_fptlCount[category] = count;
        }
    }
    barrier();

    for (uint outputCategory = 0u; outputCategory < HBG_LIGHT_CATEGORY_COUNT; ++outputCategory)
    {
        const uint listBase = outputCategory * HBG_LIGHT_LIST_MAX_LIGHTS;
        const uint count = s_fptlCount[outputCategory];
        const uint output =
            ((outputCategory * fptlCountY + uint(tile.y)) * fptlCountX + uint(tile.x)) * listStride;
        s_lightListOut[output] =
            (count & 0xffffu) | ((s_fptlList[listBase] & 0xffffu) << 16u);
        for (uint wordIndex = thread; wordIndex < listStride; wordIndex += 64u)
        {
            if (wordIndex == 0u)
            {
                continue;
            }
            const uint lowIndex = wordIndex * 2u - 1u;
            const uint highIndex = lowIndex + 1u;
            const uint low =
                lowIndex < capacity ? (s_fptlList[listBase + lowIndex] & 0xffffu) : 0u;
            const uint high =
                highIndex < capacity ? (s_fptlList[listBase + highIndex] & 0xffffu) : 0u;
            s_lightListOut[output + wordIndex] = low | (high << 16u);
        }
    }
    barrier();

    if (thread == 0u)
    {
        uint featureFlags = directionalCount > 0u ? HBG_LIGHT_FEATURE_DIRECTIONAL : 0u;
        for (uint category = 0u; category < HBG_LIGHT_CATEGORY_COUNT; ++category)
        {
            for (uint index = 0u; index < s_fptlCount[category]; ++index)
            {
                const uint lightIndex = s_fptlList[category * HBG_LIGHT_LIST_MAX_LIGHTS + index];
                featureFlags |= uint(texelFetch(s_lightData, ivec2(6, int(lightIndex)), 0).z + 0.5);
            }
        }
        const uint lastIndex = capacity - 1u;
        const uint lastLight = lastIndex < s_fptlCount[0] ? s_fptlList[lastIndex] : 0u;
        const uint featureWord =
            (lastLight & 0xffffu) | ((featureFlags & HBG_LIGHT_FEATURE_FLAGS_MASK) << 16u);
        const uint output =
            ((uint(tile.y) * fptlCountX + uint(tile.x)) * listStride) + (listStride - 1u);
        s_lightListOut[output] = featureWord;
    }
}
