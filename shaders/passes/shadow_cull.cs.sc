#include "bgfx_compute.sh"

BUFFER_RO(s_shadowRequests, vec4, 0);
BUFFER_RO(s_shadowCasters, vec4, 1);
BUFFER_WO(s_shadowInstanceData, vec4, 2);
BUFFER_RO(s_shadowBatchInfo, uint, 3);
BUFFER_WO(s_shadowIndirectArgs, uvec4, 4);
BUFFER_WO(s_shadowCullStats, uvec4, 5);

SHARED uint s_visibleCount;

NUM_THREADS(64, 1, 1)

void main()
{
    const uint requestIndex = gl_WorkGroupID.y;
    const uint batchIndex = gl_WorkGroupID.x;
    const uint requestCount = s_shadowBatchInfo[0];
    const uint batchCount = s_shadowBatchInfo[1];
    const uint casterCount = s_shadowBatchInfo[2];
    if (requestIndex >= requestCount || batchIndex >= batchCount)
    {
        return;
    }

    const uint batchBase = 4u + batchIndex * 4u;
    const uint firstCaster = s_shadowBatchInfo[batchBase + 0u];
    const uint casterInBatch = s_shadowBatchInfo[batchBase + 1u];
    const uint indexOffset = s_shadowBatchInfo[batchBase + 2u];
    const uint indexCount = s_shadowBatchInfo[batchBase + 3u];

    const uint thread = gl_LocalInvocationIndex;
    if (thread == 0u)
    {
        s_visibleCount = 0u;
    }
    barrier();

    for (uint item = thread; item < casterInBatch; item += 64u)
    {
        const uint casterIndex = firstCaster + item;
        if (casterIndex >= casterCount)
        {
            break;
        }

        const uint casterBase = casterIndex * 7u;
        const vec4 validity = s_shadowCasters[casterBase + 6u];
        bool visible = validity.x < 0.5;
        if (!visible)
        {
            visible = true;
            const vec4 center = s_shadowCasters[casterBase + 4u];
            const vec4 extents = s_shadowCasters[casterBase + 5u];
            for (int planeIndex = 0; planeIndex < 6; ++planeIndex)
            {
                const vec4 plane = s_shadowRequests[requestIndex * 6u + uint(planeIndex)];
                const float distance = dot(plane.xyz, center.xyz) + plane.w;
                const float radius = abs(plane.x) * extents.x + abs(plane.y) * extents.y +
                                     abs(plane.z) * extents.z;
                if (distance + radius < 0.0)
                {
                    visible = false;
                    break;
                }
            }
        }

        if (visible)
        {
            uint slot;
            atomicFetchAndAdd(s_visibleCount, 1u, slot);
            const uint output = (requestIndex * casterCount + firstCaster + slot) * 4u;
            s_shadowInstanceData[output + 0u] = s_shadowCasters[casterBase + 0u];
            s_shadowInstanceData[output + 1u] = s_shadowCasters[casterBase + 1u];
            s_shadowInstanceData[output + 2u] = s_shadowCasters[casterBase + 2u];
            s_shadowInstanceData[output + 3u] = s_shadowCasters[casterBase + 3u];
        }
    }
    barrier();

    if (thread == 0u)
    {
        const uint record = requestIndex * batchCount + batchIndex;
        drawIndexedIndirect(s_shadowIndirectArgs, record, indexCount, s_visibleCount, indexOffset, 0u, 0u);
        s_shadowCullStats[record * 2u + 0u] = uvec4(s_visibleCount, casterInBatch - s_visibleCount, 0u, 0u);
        s_shadowCullStats[record * 2u + 1u] = uvec4(0u, 0u, 0u, 0u);
    }
}
