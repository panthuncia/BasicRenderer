#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITDepthWarpRootConstants.h"

groupshared uint g_occupiedCount;
groupshared float g_rankScale;
groupshared uint g_histogram[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT];

[shader("compute")]
[numthreads(CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT, 1, 1)]
void CLodFixedSliceScalarVBOITDepthWarpCS(uint3 groupThreadId : SV_GroupThreadID)
{
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_CONFIG_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> occupancyHistogramBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_HISTOGRAM_DESCRIPTOR_INDEX];
    RWStructuredBuffer<float> depthWarpLUTBuffer =
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
        depthWarpLUTBuffer[sliceIndex] = (float)sliceIndex;
        return;
    }

    uint occupiedRank = 0u;
    [unroll(CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT)]
    for (uint prefixIndex = 0u; prefixIndex < CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT; ++prefixIndex)
    {
        occupiedRank += (prefixIndex <= sliceIndex && g_histogram[prefixIndex] != 0u) ? 1u : 0u;
    }

    const uint clampedRank = occupiedRank == 0u ? 0u : (occupiedRank - 1u);
    depthWarpLUTBuffer[sliceIndex] = (float)clampedRank * g_rankScale;
}