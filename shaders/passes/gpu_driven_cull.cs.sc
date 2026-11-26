#include "bgfx_compute.sh"
#include "common/common.sc"

SAMPLER2D(s_depthPyramid, 0);
BUFFER_RAW_RO(s_gpuInstances, 1);
BUFFER_RAW_RO(s_gpuBatches, 2);
BUFFER_RAW_WO(s_gpuVisibleInstances, 3);
BUFFER_RAW_WO(s_gpuIndirectArgs, 4);
BUFFER_RAW_RO(s_gpuPreviousIndirect, 5);
BUFFER_RAW_WO(s_gpuDrawCount, 6);
BUFFER_RAW_WO(s_gpuDrawRecords, 7);

uniform vec4 u_gpuDrivenParams;
uniform vec4 u_gpuDrivenCullMode;

#define HBG_GPU_INSTANCE_WORDS 36u
#define HBG_GPU_OUTPUT_WORDS 20u

SHARED uint s_visibleCount;

vec4 hbgLoadVec4(uint base)
{
    return vec4(rawLoadFloat(s_gpuInstances, base + 0u), rawLoadFloat(s_gpuInstances, base + 1u),
                rawLoadFloat(s_gpuInstances, base + 2u), rawLoadFloat(s_gpuInstances, base + 3u));
}

void hbgStoreVec4(uint base, vec4 value)
{
    rawStoreFloat(s_gpuVisibleInstances, base + 0u, value.x);
    rawStoreFloat(s_gpuVisibleInstances, base + 1u, value.y);
    rawStoreFloat(s_gpuVisibleInstances, base + 2u, value.z);
    rawStoreFloat(s_gpuVisibleInstances, base + 3u, value.w);
}

bool hbgFrustumVisible(vec3 center, vec3 extents)
{
    const float radius = length(extents);
    for (int planeIndex = 0; planeIndex < 6; ++planeIndex)
    {
        const vec4 plane = u_global_frustumPlanes[planeIndex];
        if (dot(plane.xyz, center) + plane.w < -radius)
        {
            return false;
        }
    }
    return true;
}

bool hbgHiZOccluded(vec3 center, vec3 extents)
{
    const vec3 boundsMin = center - extents;
    const vec3 boundsMax = center + extents;
    vec2 ndcMin = vec2(1.0e20, 1.0e20);
    vec2 ndcMax = vec2(-1.0e20, -1.0e20);
    float queryDepth = 1.0;

    for (int cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
    {
        const vec3 corner = vec3(
            (cornerIndex & 1) != 0 ? boundsMax.x : boundsMin.x,
            (cornerIndex & 2) != 0 ? boundsMax.y : boundsMin.y,
            (cornerIndex & 4) != 0 ? boundsMax.z : boundsMin.z);
        const vec4 clip = mul(u_global_viewProj, vec4(corner, 1.0));
        if (clip.w <= 1.0e-4)
        {
            return false;
        }
        const vec3 ndc = clip.xyz / clip.w;
        ndcMin = min(ndcMin, ndc.xy);
        ndcMax = max(ndcMax, ndc.xy);
        queryDepth = min(queryDepth, ndc.z);
    }

    vec2 uvMin = clamp(ndcMin * 0.5 + 0.5, vec2(0.0, 0.0), vec2(1.0, 1.0));
    vec2 uvMax = clamp(ndcMax * 0.5 + 0.5, vec2(0.0, 0.0), vec2(1.0, 1.0));
    if (uvMax.x <= uvMin.x || uvMax.y <= uvMin.y)
    {
        return false;
    }

    const vec2 screenSize = max(u_gpuDrivenParams.xy, vec2(1.0, 1.0));
    const float pixelExtent = max((uvMax.x - uvMin.x) * screenSize.x, (uvMax.y - uvMin.y) * screenSize.y);
    const float mipExponent = ceil(log2(max(pixelExtent * 0.5, 1.0)));
    const int maxMip = max(int(u_gpuDrivenParams.z) - 1, 0);
    const int mip = clamp(int(mipExponent), 0, maxMip);
    const vec2 mipSize = max(floor(screenSize * exp2(-float(mip))), vec2(1.0, 1.0));
    const vec2 texel = vec2(1.0, 1.0) / mipSize;
    const vec2 sampleMin = min(uvMin + texel * 0.5, uvMax - texel * 0.5);
    const vec2 sampleMax = max(uvMin + texel * 0.5, uvMax - texel * 0.5);
    const float lod = float(mip);

    const float d0 = texture2DLod(s_depthPyramid, vec2(sampleMin.x, sampleMin.y), lod).r;
    const float d1 = texture2DLod(s_depthPyramid, vec2(sampleMax.x, sampleMin.y), lod).r;
    const float d2 = texture2DLod(s_depthPyramid, vec2(sampleMin.x, sampleMax.y), lod).r;
    const float d3 = texture2DLod(s_depthPyramid, vec2(sampleMax.x, sampleMax.y), lod).r;
    const float farthestOccluder = max(max(d0, d1), max(d2, d3));
    return queryDepth > farthestOccluder + u_gpuDrivenParams.w;
}

NUM_THREADS(64, 1, 1)

void main()
{
    const uint batchIndex = gl_WorkGroupID.x;
    const uint batchBase = batchIndex * 8u;
    const uint firstItem = rawLoadUint(s_gpuBatches, batchBase + 0u);
    const uint itemCount = rawLoadUint(s_gpuBatches, batchBase + 1u);
    const uint batchIndexOffset = rawLoadUint(s_gpuBatches, batchBase + 2u);
    const uint batchIndexCount = rawLoadUint(s_gpuBatches, batchBase + 3u);
    const bool bindless = u_gpuDrivenCullMode.y > 0.5;
    const bool retestCulled = u_gpuDrivenCullMode.x > 0.5;
    const uint thread = gl_LocalInvocationIndex;

    if (thread == 0u)
    {
        s_visibleCount = 0u;
    }
    barrier();

    for (uint item = thread; item < itemCount; item += 64u)
    {
        const uint inputBase = (firstItem + item) * HBG_GPU_INSTANCE_WORDS;
        const uint validity = rawLoadUint(s_gpuInstances, inputBase + 28u);
        const bool previouslyVisible =
            !bindless && retestCulled && rawLoadUint(s_gpuPreviousIndirect, batchIndex * 8u + 1u) != 0u;
        bool visible = !previouslyVisible && validity == 0u;
        if (!previouslyVisible && !visible)
        {
            const vec4 centerData = hbgLoadVec4(inputBase + 20u);
            const vec4 extentsData = hbgLoadVec4(inputBase + 24u);
            visible = hbgFrustumVisible(centerData.xyz, extentsData.xyz) &&
                      !hbgHiZOccluded(centerData.xyz, extentsData.xyz);
        }

        if (visible)
        {
            uint slot;
            atomicFetchAndAdd(s_visibleCount, 1u, slot);
            const uint outputBase = (bindless ? slot : (firstItem + slot)) * HBG_GPU_OUTPUT_WORDS;
            hbgStoreVec4(outputBase + 0u, hbgLoadVec4(inputBase + 0u));
            hbgStoreVec4(outputBase + 4u, hbgLoadVec4(inputBase + 4u));
            hbgStoreVec4(outputBase + 8u, hbgLoadVec4(inputBase + 8u));
            hbgStoreVec4(outputBase + 12u, hbgLoadVec4(inputBase + 12u));
            hbgStoreVec4(outputBase + 16u, hbgLoadVec4(inputBase + 16u));
            if (bindless)
            {
                const uint materialIndex = rawLoadUint(s_gpuInstances, inputBase + 29u);
                const uint indexOffset = rawLoadUint(s_gpuInstances, inputBase + 30u);
                const uint indexCount = rawLoadUint(s_gpuInstances, inputBase + 31u);
                const uint vertexBase = rawLoadUint(s_gpuInstances, inputBase + 32u);
                rawStoreFloat(s_gpuVisibleInstances, outputBase + 19u, float(materialIndex));

                const uint recordBase = slot * 8u;
                rawStoreUint(s_gpuIndirectArgs, recordBase + 0u, indexCount);
                rawStoreUint(s_gpuIndirectArgs, recordBase + 1u, 1u);
                rawStoreUint(s_gpuIndirectArgs, recordBase + 2u,
                             floatBitsToUint(bgfx_indirectArgBase.x) + indexOffset);
                rawStoreUint(s_gpuIndirectArgs, recordBase + 3u, vertexBase);
                rawStoreUint(s_gpuIndirectArgs, recordBase + 4u, slot);
                // Keep the absolute draw record and source index for the transparent
                // depth-sort pass. Indirect buffers are UAV-only in bgfx, so the sort
                // pass reads this compute-readable mirror instead.
                const uint recordOffset = slot * 4u;
                rawStoreUint(s_gpuDrawRecords, recordOffset + 0u, indexCount);
                rawStoreUint(s_gpuDrawRecords, recordOffset + 1u, indexOffset);
                rawStoreUint(s_gpuDrawRecords, recordOffset + 2u, vertexBase);
                rawStoreUint(s_gpuDrawRecords, recordOffset + 3u, firstItem + item);
            }
            else
            {
            }
        }
    }
    barrier();

    if (bindless)
    {
        for (uint item = thread; item < itemCount; item += 64u)
        {
            if (item >= s_visibleCount)
            {
                const uint recordBase = item * 8u;
                for (uint word = 0u; word < 8u; ++word)
                {
                    rawStoreUint(s_gpuIndirectArgs, recordBase + word, 0u);
                }
            }
        }
        if (thread == 0u)
        {
            rawStoreUint(s_gpuDrawCount, batchIndex, s_visibleCount);
        }
    }
    else if (thread == 0u)
    {
        const uint recordBase = batchIndex * 8u;
        rawStoreUint(s_gpuIndirectArgs, recordBase + 0u, batchIndexCount);
        rawStoreUint(s_gpuIndirectArgs, recordBase + 1u, s_visibleCount);
        rawStoreUint(s_gpuIndirectArgs, recordBase + 2u,
                     floatBitsToUint(bgfx_indirectArgBase.x) + batchIndexOffset);
        rawStoreUint(s_gpuIndirectArgs, recordBase + 3u, 0u);
        rawStoreUint(s_gpuIndirectArgs, recordBase + 4u, 0u);
        rawStoreUint(s_gpuDrawCount, batchIndex, s_visibleCount);
    }
}
