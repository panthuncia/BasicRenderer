#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITDepthWarpRootConstants.h"

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodFixedSliceScalarVBOITOccupancyRemapCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_CONFIG_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodFixedSliceScalarVBOITDepthWarpLUTEntry> depthWarpLUTBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_LUT_DESCRIPTOR_INDEX];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];

    if (config.sliceCount == 0u ||
        config.virtualSliceCount == 0u ||
        dispatchThreadId.x >= config.lowResolutionWidth ||
        dispatchThreadId.y >= config.lowResolutionHeight ||
        config.occupancyUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.occupancySliceMaskUAVDescriptorIndex == 0xFFFFFFFFu)
    {
        return;
    }

    RWTexture2D<float> occupancyTexture = ResourceDescriptorHeap[config.occupancyUAVDescriptorIndex];
    RWTexture2D<uint> occupancySliceMaskTexture = ResourceDescriptorHeap[config.occupancySliceMaskUAVDescriptorIndex];

    const uint2 lowPixel = dispatchThreadId.xy;
    if (occupancyTexture[lowPixel] <= 0.0f)
    {
        return;
    }

    const uint sourceSliceMask = occupancySliceMaskTexture[lowPixel];
    if (sourceSliceMask == 0u)
    {
        return;
    }

    uint remappedSliceMask = 0u;
    [loop]
    for (uint sourceSliceIndex = 0u; sourceSliceIndex < config.virtualSliceCount; ++sourceSliceIndex)
    {
        const uint sourceSliceBit = 1u << sourceSliceIndex;
        if ((sourceSliceMask & sourceSliceBit) == 0u)
        {
            continue;
        }

        const CLodFixedSliceScalarVBOITDepthWarpLUTEntry depthWarpEntry = depthWarpLUTBuffer[sourceSliceIndex];
        float minWarpedSliceCoordinate = depthWarpEntry.warpedSliceCoordinate;
        float maxWarpedSliceCoordinate = depthWarpEntry.warpedSliceCoordinate;
        if (sourceSliceIndex + 1u < config.virtualSliceCount &&
            (depthWarpEntry.flags & CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_FLAG_FILTER_TO_NEXT) != 0u)
        {
            const CLodFixedSliceScalarVBOITDepthWarpLUTEntry nextDepthWarpEntry = depthWarpLUTBuffer[sourceSliceIndex + 1u];
            minWarpedSliceCoordinate = min(minWarpedSliceCoordinate, nextDepthWarpEntry.warpedSliceCoordinate);
            maxWarpedSliceCoordinate = max(maxWarpedSliceCoordinate, nextDepthWarpEntry.warpedSliceCoordinate);
        }

        const uint remappedSliceIndexMin = min((uint)floor(minWarpedSliceCoordinate), config.sliceCount - 1u);
        const uint remappedSliceIndexMax = min((uint)ceil(maxWarpedSliceCoordinate), config.sliceCount - 1u);
        [loop]
        for (uint remappedSliceIndex = remappedSliceIndexMin; remappedSliceIndex <= remappedSliceIndexMax; ++remappedSliceIndex)
        {
            remappedSliceMask |= 1u << remappedSliceIndex;
        }
    }

    occupancySliceMaskTexture[lowPixel] = remappedSliceMask;
}