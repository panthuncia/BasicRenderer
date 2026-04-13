#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "PerPassRootConstants/clodAVBOITOccupancyHistogramRootConstants.h"

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodAVBOITOccupancyHistogramCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<CLodAVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_OCCUPANCY_HISTOGRAM_CONFIG_DESCRIPTOR_INDEX];
    const CLodAVBOITConfig config = configBuffer[0];

    if (config.virtualSliceCount == 0u ||
        dispatchThreadId.x >= config.lowResolutionWidth ||
        dispatchThreadId.y >= config.lowResolutionHeight ||
        config.occupancyUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.occupancySliceMaskUAVDescriptorIndex == 0xFFFFFFFFu ||
        CLOD_AVBOIT_VBOIT_OCCUPANCY_HISTOGRAM_BUFFER_DESCRIPTOR_INDEX == 0xFFFFFFFFu)
    {
        return;
    }

    RWTexture2D<float> occupancyTexture = ResourceDescriptorHeap[config.occupancyUAVDescriptorIndex];
    RWTexture2D<uint> occupancySliceMaskTexture = ResourceDescriptorHeap[config.occupancySliceMaskUAVDescriptorIndex];
    RWStructuredBuffer<uint> occupancyHistogramBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_OCCUPANCY_HISTOGRAM_BUFFER_DESCRIPTOR_INDEX];

    const uint2 lowPixel = dispatchThreadId.xy;
    if (occupancyTexture[lowPixel] <= 0.0f)
    {
        return;
    }

    uint remainingSliceMask = occupancySliceMaskTexture[lowPixel];
    while (remainingSliceMask != 0u)
    {
        const uint sliceIndex = firstbitlow(remainingSliceMask);
        if (sliceIndex >= config.virtualSliceCount)
        {
            break;
        }

        InterlockedAdd(occupancyHistogramBuffer[sliceIndex], 1u);
        remainingSliceMask &= (remainingSliceMask - 1u);
    }
}