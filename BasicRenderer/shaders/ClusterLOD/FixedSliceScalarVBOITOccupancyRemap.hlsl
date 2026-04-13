#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITDepthWarpRootConstants.h"

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodFixedSliceScalarVBOITOccupancyRemapCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_CONFIG_DESCRIPTOR_INDEX];
    StructuredBuffer<float> depthWarpLUTBuffer =
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

        const float warpedSliceCoordinate = depthWarpLUTBuffer[sourceSliceIndex];
        const uint warpedSliceIndex0 = min((uint)floor(warpedSliceCoordinate), config.sliceCount - 1u);
        const uint warpedSliceIndex1 = min(warpedSliceIndex0 + 1u, config.sliceCount - 1u);
        remappedSliceMask |= 1u << warpedSliceIndex0;
        remappedSliceMask |= 1u << warpedSliceIndex1;
    }

    occupancySliceMaskTexture[lowPixel] = remappedSliceMask;
}