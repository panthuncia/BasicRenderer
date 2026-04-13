#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/AVBOITCommon.hlsli"
#include "PerPassRootConstants/clodAVBOITDepthWarpRootConstants.h"

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodAVBOITOccupancyRemapCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<CLodAVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_DEPTH_WARP_CONFIG_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodAVBOITDepthWarpLUTEntry> depthWarpLUTBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_DEPTH_WARP_LUT_DESCRIPTOR_INDEX];
    const CLodAVBOITConfig config = configBuffer[0];

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

        const float sourceSliceCoordinateMin = (float)sourceSliceIndex;
        const float sourceSliceCoordinateMax = min((float)(sourceSliceIndex + 1u), (float)(config.virtualSliceCount - 1u));
        const float warpedSliceCoordinateMin = SampleDepthWarpLUT(
            depthWarpLUTBuffer,
            ComputeDepthWarpLUTSampleCoordinate(config, sourceSliceCoordinateMin));
        const float warpedSliceCoordinateMax = SampleDepthWarpLUT(
            depthWarpLUTBuffer,
            ComputeDepthWarpLUTSampleCoordinate(config, sourceSliceCoordinateMax));
        const float minWarpedSliceCoordinate = min(warpedSliceCoordinateMin, warpedSliceCoordinateMax);
        const float maxWarpedSliceCoordinate = max(warpedSliceCoordinateMin, warpedSliceCoordinateMax);

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