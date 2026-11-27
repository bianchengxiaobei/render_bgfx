#include "bgfx_compute.sh"
#include "common/common.sc"

BUFFER_RO(s_lightScreenBounds, vec4, 1);
BUFFER_RO(s_lightBigTileList, uint, 2);
BUFFER_WO(s_lightClusterListOut, uint, 3);

#define HBG_LIGHT_LIST_MAX_LIGHTS 128u
#define HBG_LIGHT_LIST_MASK_WORDS 4u
#define HBG_LIGHT_CATEGORY_COUNT 2u
#define HBG_LIGHT_CLUSTER_SLICE_COUNT 32u
#define HBG_LIGHT_CLUSTER_OFFSET_BITS 24u
#define HBG_LIGHT_CLUSTER_OFFSET_MASK 0x00ffffffu
#define HBG_LIGHT_FEATURE_FLAGS_MASK 0xffffu

SHARED uint s_clusterMask[HBG_LIGHT_CATEGORY_COUNT * HBG_LIGHT_LIST_MASK_WORDS];
SHARED uint s_clusterCount[HBG_LIGHT_CATEGORY_COUNT];
SHARED uint s_clusterList[HBG_LIGHT_CATEGORY_COUNT * HBG_LIGHT_LIST_MAX_LIGHTS];

int hbgClusterSliceForDepth(float linearDepth)
{
    const float nearPlane = max(u_cluster_depthParams.x, 0.0001);
    const float farPlane = max(u_cluster_depthParams.y, nearPlane + 0.0001);
    const float rangeFittedDistance = hbgSaturate((linearDepth - nearPlane) / (farPlane - nearPlane));
    const float clusterCount = float(HBG_LIGHT_CLUSTER_SLICE_COUNT);
    const float logBase = max(u_cluster_depthParams.z, 1.0001);
    const float scale = pow(logBase, clusterCount);
    const float slice = log(1.0 + rangeFittedDistance * (scale - 1.0)) / log(logBase);
    return clamp(int(floor(slice)), 0, int(clusterCount) - 1);
}

NUM_THREADS(64, 1, 1)

void main()
{
    const uint thread = gl_LocalInvocationIndex;
    const ivec2 tile = ivec2(gl_WorkGroupID.xy);
    const int slice = int(gl_WorkGroupID.z);
    if (thread == 0u)
    {
        for (uint category = 0u; category < HBG_LIGHT_CATEGORY_COUNT; ++category)
        {
            s_clusterCount[category] = 0u;
        }
        for (uint word = 0u; word < HBG_LIGHT_CATEGORY_COUNT * HBG_LIGHT_LIST_MASK_WORDS; ++word)
        {
            s_clusterMask[word] = 0u;
        }
    }
    for (uint clearIndex = thread; clearIndex < HBG_LIGHT_CATEGORY_COUNT * HBG_LIGHT_LIST_MAX_LIGHTS;
         clearIndex += 64u)
    {
        s_clusterList[clearIndex] = 0u;
    }
    barrier();

    const uint capacity =
        min(uint(max(u_cluster_lightListParams2.y, 0.0) + 0.5), HBG_LIGHT_LIST_MAX_LIGHTS);
    const uint listStride = max(uint(max(u_cluster_lightListParams2.x, 0.0) + 0.5), 1u);
    const uint offsetBase = uint(max(u_cluster_params2.y, 0.0) + 0.5);
    const uint clusterCountX = max(uint(int(u_cluster_params.y + 0.5)), 1u);
    const uint clusterCountY = max(uint(int(u_cluster_params.z + 0.5)), 1u);
    const uint clusterCapacity = uint(max(u_cluster_lightListParams2.y, 0.0) + 0.5);
    const uint clusterCount = clusterCountX * clusterCountY * HBG_LIGHT_CLUSTER_SLICE_COUNT;
    const float tileSize = max(u_cluster_params.x, 1.0);
    const vec2 screenSize = max(u_cluster_screenSize.xy, vec2(1.0, 1.0));
    const vec2 tileMin = vec2(float(tile.x) * tileSize, float(tile.y) * tileSize);
    const vec2 tileMax = min(tileMin + vec2(tileSize, tileSize), screenSize);
    const float nearPlane = max(u_cluster_depthParams.x, 0.0001);

    for (uint category = 0u; category < HBG_LIGHT_CATEGORY_COUNT; ++category)
    {
        const uint bigTileBase =
            ((category * clusterCountY + uint(tile.y)) * clusterCountX + uint(tile.x)) * listStride;
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
            if (maxBounds.z <= 0.5)
            {
                continue;
            }
            if (!(maxBounds.x > tileMin.x && minBounds.x < tileMax.x &&
                  maxBounds.y > tileMin.y && minBounds.y < tileMax.y))
            {
                continue;
            }

            const float minDepth = max(minBounds.z, nearPlane);
            const float maxDepth = max(minBounds.w, minDepth);
            if (maxDepth < nearPlane)
            {
                continue;
            }
            const int minSlice = hbgClusterSliceForDepth(minDepth);
            const int maxSlice = hbgClusterSliceForDepth(maxDepth);
            if (slice >= minSlice && slice <= maxSlice)
            {
                atomicOr(s_clusterMask[maskBase + (lightIndex >> 5u)], 1u << (lightIndex & 31u));
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
                if ((s_clusterMask[maskBase + (index >> 5u)] & (1u << (index & 31u))) != 0u)
                {
                    s_clusterList[listBase + count] = index;
                    ++count;
                }
            }
            s_clusterCount[category] = count;

            const uint offsetIndex =
                ((category * HBG_LIGHT_CLUSTER_SLICE_COUNT + uint(slice)) * clusterCountY +
                 uint(tile.y)) *
                    clusterCountX +
                uint(tile.x);
            const uint clusterOffset =
                (category * clusterCount +
                 (uint(slice) * clusterCountY + uint(tile.y)) * clusterCountX + uint(tile.x)) *
                clusterCapacity;
            s_lightClusterListOut[offsetIndex] =
                (clusterOffset & HBG_LIGHT_CLUSTER_OFFSET_MASK) |
                ((count & 0xffu) << HBG_LIGHT_CLUSTER_OFFSET_BITS);
        }
    }
    barrier();

    for (uint outputCategory = 0u; outputCategory < HBG_LIGHT_CATEGORY_COUNT; ++outputCategory)
    {
        const uint listBase = outputCategory * HBG_LIGHT_LIST_MAX_LIGHTS;
        const uint count = s_clusterCount[outputCategory];
        const uint clusterOffset =
            (outputCategory * clusterCount +
             (uint(slice) * clusterCountY + uint(tile.y)) * clusterCountX + uint(tile.x)) *
            clusterCapacity;
        if (count > 0u)
        {
            for (uint index = thread; index < count; index += 64u)
            {
                s_lightClusterListOut[offsetBase + clusterOffset + index] =
                    s_clusterList[listBase + index];
            }
        }
    }

    if (thread == 0u)
    {
        const uint bigTileFlagWord =
            s_lightBigTileList[((uint(tile.y) * clusterCountX + uint(tile.x)) * listStride) +
                               (listStride - 1u)];
        const uint featureFlags = (bigTileFlagWord >> 16u) & HBG_LIGHT_FEATURE_FLAGS_MASK;
        const uint flagBase = uint(max(u_cluster_params2.y, 0.0) + 0.5) +
                              uint(max(u_cluster_params2.z, 0.0) + 0.5);
        const uint position = flagBase + uint(tile.y) * clusterCountX + uint(tile.x);
        s_lightClusterListOut[position] = featureFlags;
    }
}
