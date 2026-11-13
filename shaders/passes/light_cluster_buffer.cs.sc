#include "bgfx_compute.sh"
#include "common/common.sc"

BUFFER_RO(s_lightScreenBounds, vec4, 1);
BUFFER_RO(s_lightBigTileList, uint, 2);
BUFFER_WO(s_lightClusterListOut, uint, 3);

#define HBG_LIGHT_CLUSTER_STRIDE 33u
#define HBG_LIGHT_CLUSTER_MAX_LIGHTS 32u
#define HBG_LIGHT_CLUSTER_SLICE_COUNT 32u

SHARED uint s_clusterMask;
SHARED uint s_clusterCount;
SHARED uint s_clusterList[32];

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
        s_clusterMask = 0u;
        s_clusterCount = 0u;
    }
    barrier();

    const uint clusterCountX = max(uint(int(u_cluster_params.y + 0.5)), 1u);
    const uint bigTileBase = (uint(tile.y) * clusterCountX + uint(tile.x)) * HBG_LIGHT_CLUSTER_STRIDE;
    const uint bigTileLightCount = min(s_lightBigTileList[bigTileBase], HBG_LIGHT_CLUSTER_MAX_LIGHTS);
    const float tileSize = max(u_cluster_params.x, 1.0);
    const vec2 screenSize = max(u_cluster_screenSize.xy, vec2(1.0, 1.0));
    const vec2 tileMin = vec2(float(tile.x) * tileSize, float(tile.y) * tileSize);
    const vec2 tileMax = min(tileMin + vec2(tileSize, tileSize), screenSize);
    const float nearPlane = max(u_cluster_depthParams.x, 0.0001);

    for (uint offset = thread; offset < bigTileLightCount; offset += 64u)
    {
        const uint lightIndex = s_lightBigTileList[bigTileBase + offset + 1u];
        if (lightIndex >= HBG_LIGHT_CLUSTER_MAX_LIGHTS)
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
            atomicOr(s_clusterMask, 1u << lightIndex);
        }
    }
    barrier();

    if (thread == 0u)
    {
        uint count = 0u;
        for (uint index = 0u; index < HBG_LIGHT_CLUSTER_MAX_LIGHTS; ++index)
        {
            if ((s_clusterMask & (1u << index)) != 0u)
            {
                s_clusterList[count] = index;
                ++count;
            }
        }
        for (uint clearIndex = count; clearIndex < HBG_LIGHT_CLUSTER_MAX_LIGHTS; ++clearIndex)
        {
            s_clusterList[clearIndex] = 0u;
        }
        s_clusterCount = count;
    }
    barrier();

    const uint clusterIndex =
        (uint(tile.y) * HBG_LIGHT_CLUSTER_SLICE_COUNT + uint(slice)) * (clusterCountX * HBG_LIGHT_CLUSTER_STRIDE) +
        uint(tile.x) * HBG_LIGHT_CLUSTER_STRIDE;
    if (thread == 0u)
    {
        s_lightClusterListOut[clusterIndex] = s_clusterCount;
    }
    if (thread < HBG_LIGHT_CLUSTER_MAX_LIGHTS)
    {
        s_lightClusterListOut[clusterIndex + thread + 1u] = s_clusterList[thread];
    }
}
