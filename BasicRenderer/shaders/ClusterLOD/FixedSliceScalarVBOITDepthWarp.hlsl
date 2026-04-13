#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITDepthWarpRootConstants.h"

groupshared uint g_occupiedCount;
groupshared float g_rankScale;
groupshared uint g_histogram[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT];

float ComputeMappedRankCoordinate(uint rank, float rankScale)
{
    return (float)rank * rankScale;
}

[shader("compute")]
[numthreads(CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT, 1, 1)]
void CLodFixedSliceScalarVBOITDepthWarpCS(uint3 groupThreadId : SV_GroupThreadID)
{
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_CONFIG_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> occupancyHistogramBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_HISTOGRAM_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodFixedSliceScalarVBOITDepthWarpLUTEntry> depthWarpLUTBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_LUT_DESCRIPTOR_INDEX];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];
    const uint sliceIndex = groupThreadId.x;

    if (sliceIndex >= CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT)
    {
        return;
    }

    const bool sliceActive = sliceIndex < config.virtualSliceCount;
    const uint histogramValue = sliceActive ? occupancyHistogramBuffer[sliceIndex] : 0u;
    g_histogram[sliceIndex] = histogramValue;

    if (sliceIndex == 0u)
    {
        g_occupiedCount = 0u;
        g_rankScale = 0.0f;
    }

    GroupMemoryBarrierWithGroupSync();

    if (sliceActive && histogramValue != 0u)
    {
        InterlockedAdd(g_occupiedCount, 1u);
    }

    GroupMemoryBarrierWithGroupSync();

    if (sliceIndex == 0u)
    {
        g_rankScale = g_occupiedCount > 1u
            ? (float)(config.sliceCount - 1u) / (float)(g_occupiedCount - 1u)
            : 0.0f;
    }

    GroupMemoryBarrierWithGroupSync();

    if (!sliceActive || g_occupiedCount < 2u)
    {
        CLodFixedSliceScalarVBOITDepthWarpLUTEntry depthWarpEntry;
        depthWarpEntry.warpedSliceCoordinate = (float)sliceIndex;
        depthWarpEntry.flags = (sliceActive && sliceIndex + 1u < config.virtualSliceCount &&
            histogramValue != 0u && g_histogram[sliceIndex + 1u] != 0u)
            ? CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_FLAG_FILTER_TO_NEXT
            : 0u;
        depthWarpLUTBuffer[sliceIndex] = depthWarpEntry;
        return;
    }

    const bool sliceOccupied = histogramValue != 0u;
    uint occupiedRank = 0u;
    uint previousOccupiedRank = 0u;
    uint nextOccupiedRank = 0u;
    bool hasPreviousOccupiedSlice = false;
    bool hasNextOccupiedSlice = false;
    [unroll(CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT)]
    for (uint prefixIndex = 0u; prefixIndex < CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT; ++prefixIndex)
    {
        if (prefixIndex >= config.virtualSliceCount || g_histogram[prefixIndex] == 0u)
        {
            continue;
        }

        const uint candidateRank = occupiedRank;
        if (prefixIndex <= sliceIndex)
        {
            previousOccupiedRank = candidateRank;
            hasPreviousOccupiedSlice = true;
        }
        else if (!hasNextOccupiedSlice)
        {
            nextOccupiedRank = candidateRank;
            hasNextOccupiedSlice = true;
        }

        occupiedRank++;
    }

    uint mappedRank = 0u;
    if (sliceOccupied || hasPreviousOccupiedSlice)
    {
        mappedRank = previousOccupiedRank;
    }
    else if (hasNextOccupiedSlice)
    {
        mappedRank = nextOccupiedRank;
    }

    CLodFixedSliceScalarVBOITDepthWarpLUTEntry depthWarpEntry;
    depthWarpEntry.warpedSliceCoordinate = ComputeMappedRankCoordinate(mappedRank, g_rankScale);
    depthWarpEntry.flags = (sliceOccupied && sliceIndex + 1u < config.virtualSliceCount &&
        g_histogram[sliceIndex + 1u] != 0u)
        ? CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_FLAG_FILTER_TO_NEXT
        : 0u;
    depthWarpLUTBuffer[sliceIndex] = depthWarpEntry;
}