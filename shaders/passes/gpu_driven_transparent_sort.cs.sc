#include "bgfx_compute.sh"

BUFFER_RAW_RO(s_gpuInstances, 1);
BUFFER_RAW_RO(s_gpuVisibleInstances, 2);
BUFFER_RAW_RO(s_gpuDrawCount, 3);
BUFFER_RAW_RO(s_gpuDrawRecords, 4);
BUFFER_RAW_WO(s_gpuSortedVisible, 5);
BUFFER_RAW_WO(s_gpuSortedIndirect, 6);

uniform vec4 u_gpuSortParams;

#define HBG_GPU_INSTANCE_WORDS 36u
#define HBG_GPU_OUTPUT_WORDS 20u

bool hbgTransparentKeyLess(uint a0, uint a1, uint a2, uint aIndex, uint b0, uint b1, uint b2, uint bIndex)
{
    if (a0 != b0)
    {
        return a0 < b0;
    }
    if (a1 != b1)
    {
        return a1 < b1;
    }
    if (a2 != b2)
    {
        return a2 < b2;
    }
    return aIndex < bIndex;
}

void hbgZeroIndirect(uint item)
{
    const uint base = item * 8u;
    for (uint word = 0u; word < 8u; ++word)
    {
        rawStoreUint(s_gpuSortedIndirect, base + word, 0u);
    }
}

NUM_THREADS(64, 1, 1)

void main()
{
    const uint item = gl_GlobalInvocationID.x;
    const uint itemCount = uint(u_gpuSortParams.x + 0.5);
    if (item >= itemCount)
    {
        return;
    }

    const uint visibleCount = rawLoadUint(s_gpuDrawCount, 0u);
    if (item >= visibleCount)
    {
        hbgZeroIndirect(item);
        return;
    }

    const uint recordBase = item * 4u;
    const uint indexCount = rawLoadUint(s_gpuDrawRecords, recordBase + 0u);
    const uint indexOffset = rawLoadUint(s_gpuDrawRecords, recordBase + 1u);
    const uint vertexBase = rawLoadUint(s_gpuDrawRecords, recordBase + 2u);
    const uint originalIndex = rawLoadUint(s_gpuDrawRecords, recordBase + 3u);
    const uint keyBase = originalIndex * HBG_GPU_INSTANCE_WORDS + 33u;
    const uint key0 = rawLoadUint(s_gpuInstances, keyBase + 0u);
    const uint key1 = rawLoadUint(s_gpuInstances, keyBase + 1u);
    const uint key2 = rawLoadUint(s_gpuInstances, keyBase + 2u);

    uint rank = 0u;
    for (uint other = 0u; other < visibleCount; ++other)
    {
        const uint otherBase = other * 4u;
        const uint otherIndex = rawLoadUint(s_gpuDrawRecords, otherBase + 3u);
        const uint otherKeyBase = otherIndex * HBG_GPU_INSTANCE_WORDS + 33u;
        if (hbgTransparentKeyLess(key0, key1, key2, originalIndex,
                                  rawLoadUint(s_gpuInstances, otherKeyBase + 0u),
                                  rawLoadUint(s_gpuInstances, otherKeyBase + 1u),
                                  rawLoadUint(s_gpuInstances, otherKeyBase + 2u), otherIndex))
        {
            ++rank;
        }
    }

    const uint visibleBase = item * HBG_GPU_OUTPUT_WORDS;
    const uint sortedVisibleBase = rank * HBG_GPU_OUTPUT_WORDS;
    for (uint visibleWord = 0u; visibleWord < HBG_GPU_OUTPUT_WORDS; ++visibleWord)
    {
        rawStoreFloat(s_gpuSortedVisible, sortedVisibleBase + visibleWord,
                      rawLoadFloat(s_gpuVisibleInstances, visibleBase + visibleWord));
    }

    const uint sortedIndirectBase = rank * 8u;
    rawStoreUint(s_gpuSortedIndirect, sortedIndirectBase + 0u, indexCount);
    rawStoreUint(s_gpuSortedIndirect, sortedIndirectBase + 1u, 1u);
    rawStoreUint(s_gpuSortedIndirect, sortedIndirectBase + 2u, indexOffset);
    rawStoreUint(s_gpuSortedIndirect, sortedIndirectBase + 3u, vertexBase);
    rawStoreUint(s_gpuSortedIndirect, sortedIndirectBase + 4u, rank);
    rawStoreUint(s_gpuSortedIndirect, sortedIndirectBase + 5u, originalIndex);
    rawStoreUint(s_gpuSortedIndirect, sortedIndirectBase + 6u, 0u);
    rawStoreUint(s_gpuSortedIndirect, sortedIndirectBase + 7u, 0u);
}