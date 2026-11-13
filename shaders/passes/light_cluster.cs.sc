#include "bgfx_compute.sh"
#include "common/common.sc"

SAMPLER2D(s_lightScreenBounds, 1);
USAMPLER2D(s_lightBigTileList, 2);
SAMPLER2D(s_lightData, 5);
SAMPLER2D(s_lightViewRows, 6);
UIMAGE2D_WO(s_lightClusterListOut, r32ui, 3);

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
    }
    barrier();

    const uint bigTileLightCount =
        min(texelFetch(s_lightBigTileList, ivec2(tile.x * int(HBG_LIGHT_CLUSTER_STRIDE), tile.y), 0).x,
            HBG_LIGHT_CLUSTER_MAX_LIGHTS);
    const float tileSize = max(u_cluster_params.x, 1.0);
    const vec2 screenSize = max(u_cluster_screenSize.xy, vec2(1.0, 1.0));
    const vec2 tileMin = vec2(float(tile.x) * tileSize, float(tile.y) * tileSize);
    const vec2 tileMax = min(tileMin + vec2(tileSize, tileSize), screenSize);
    const float nearPlane = max(u_cluster_depthParams.x, 0.0001);
    const vec4 viewRow2 = texelFetch(s_lightViewRows, ivec2(4, 0), 0);

    for (uint offset = thread; offset < bigTileLightCount; offset += 64u)
    {
        const uint lightIndex =
            texelFetch(s_lightBigTileList,
                       ivec2(tile.x * int(HBG_LIGHT_CLUSTER_STRIDE) + int(offset) + 1, tile.y), 0)
                .x;
        if (lightIndex >= HBG_LIGHT_CLUSTER_MAX_LIGHTS)
        {
            continue;
        }

        const vec4 minBounds = texelFetch(s_lightScreenBounds, ivec2(int(lightIndex * 2u), 0), 0);
        const vec4 maxBounds = texelFetch(s_lightScreenBounds, ivec2(int(lightIndex * 2u + 1u), 0), 0);
        if (maxBounds.z <= 0.5)
        {
            continue;
        }
        if (!(maxBounds.x > tileMin.x && minBounds.x < tileMax.x &&
              maxBounds.y > tileMin.y && minBounds.y < tileMax.y))
        {
            continue;
        }

        const vec4 positionRange = texelFetch(s_lightData, ivec2(0, int(lightIndex)), 0);
        const float centerDepth = -dot(viewRow2, vec4(positionRange.xyz, 1.0));
        const float range = max(positionRange.w, 0.0);
        if (centerDepth + range < nearPlane)
        {
            continue;
        }
        const int minSlice = hbgClusterSliceForDepth(centerDepth - range);
        const int maxSlice = hbgClusterSliceForDepth(centerDepth + range);
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

    const int sliceCount = int(HBG_LIGHT_CLUSTER_SLICE_COUNT);
    const ivec2 output = ivec2(tile.x * int(HBG_LIGHT_CLUSTER_STRIDE), tile.y * sliceCount + slice);
    if (thread == 0u)
    {
        imageStore(s_lightClusterListOut, output, uvec4(s_clusterCount, 0u, 0u, 0u));
    }
    if (thread < HBG_LIGHT_CLUSTER_MAX_LIGHTS)
    {
        imageStore(s_lightClusterListOut, output + ivec2(int(thread) + 1, 0),
                   uvec4(s_clusterList[thread], 0u, 0u, 0u));
    }
}
