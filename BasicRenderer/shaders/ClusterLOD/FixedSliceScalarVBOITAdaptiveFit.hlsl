#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITAdaptiveFitRootConstants.h"

[shader("compute")]
[numthreads(1, 1, 1)]
void CLodFixedSliceScalarVBOITAdaptiveFitCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u || dispatchThreadId.y != 0u || dispatchThreadId.z != 0u)
    {
        return;
    }

    RWStructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_ADAPTIVE_FIT_CONFIG_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodFixedSliceScalarVBOITFitState> fitStateBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_ADAPTIVE_FIT_STATE_DESCRIPTOR_INDEX];

    CLodFixedSliceScalarVBOITConfig config = configBuffer[0];
    const CLodFixedSliceScalarVBOITFitState fitState = fitStateBuffer[0];
    const uint minVirtualSliceCount = max(config.sliceCount, 1u);
    const uint maxVirtualSliceCount = CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT;
    uint fittedVirtualSliceCount = fitState.fittedVirtualSliceCount;
    if (fittedVirtualSliceCount < minVirtualSliceCount || fittedVirtualSliceCount > maxVirtualSliceCount)
    {
        fittedVirtualSliceCount = max(config.virtualSliceCount, minVirtualSliceCount);
    }

    config.virtualSliceCount = clamp(fittedVirtualSliceCount, minVirtualSliceCount, maxVirtualSliceCount);
    configBuffer[0] = config;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void CLodFixedSliceScalarVBOITAdaptiveFitUpdateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u || dispatchThreadId.y != 0u || dispatchThreadId.z != 0u)
    {
        return;
    }

    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_ADAPTIVE_FIT_CONFIG_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> occupancyHistogramBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_ADAPTIVE_FIT_HISTOGRAM_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodFixedSliceScalarVBOITFitState> fitStateBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_ADAPTIVE_FIT_STATE_DESCRIPTOR_INDEX];

    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];
    const uint minVirtualSliceCount = max(config.sliceCount, 1u);
    const uint maxVirtualSliceCount = CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT;
    uint occupiedVirtualSliceCount = 0u;
    [unroll(CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT)]
    for (uint sliceIndex = 0u; sliceIndex < CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT; ++sliceIndex)
    {
        if (sliceIndex >= config.virtualSliceCount)
        {
            break;
        }

        occupiedVirtualSliceCount += occupancyHistogramBuffer[sliceIndex] != 0u ? 1u : 0u;
    }

    uint fittedVirtualSliceCount = clamp(config.virtualSliceCount, minVirtualSliceCount, maxVirtualSliceCount);
    if (occupiedVirtualSliceCount > config.sliceCount)
    {
        fittedVirtualSliceCount = max(
            minVirtualSliceCount,
            (fittedVirtualSliceCount * config.sliceCount + occupiedVirtualSliceCount - 1u) / occupiedVirtualSliceCount);
    }
    else if (occupiedVirtualSliceCount * 2u < config.sliceCount && fittedVirtualSliceCount < maxVirtualSliceCount)
    {
        fittedVirtualSliceCount = min(maxVirtualSliceCount, fittedVirtualSliceCount * 2u);
    }

    CLodFixedSliceScalarVBOITFitState fitState;
    fitState.fittedVirtualSliceCount = clamp(fittedVirtualSliceCount, minVirtualSliceCount, maxVirtualSliceCount);
    fitState.occupiedVirtualSliceCount = occupiedVirtualSliceCount;
    fitState.pad0 = 0u;
    fitState.pad1 = 0u;
    fitStateBuffer[0] = fitState;
}