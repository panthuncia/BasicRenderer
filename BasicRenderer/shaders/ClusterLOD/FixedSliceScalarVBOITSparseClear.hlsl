#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITIntegrateRootConstants.h"

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodFixedSliceScalarVBOITSparseClearCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_CONFIG_DESCRIPTOR_INDEX];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];

    if (config.sliceCount == 0u ||
        dispatchThreadId.x >= config.lowResolutionWidth ||
        dispatchThreadId.y >= config.lowResolutionHeight ||
        config.occupancyUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.occupancySliceMaskUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.extinctionUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.zeroTransmittanceSliceUAVDescriptorIndex == 0xFFFFFFFFu)
    {
        return;
    }

    RWTexture2D<float> occupancyTexture = ResourceDescriptorHeap[config.occupancyUAVDescriptorIndex];
    RWTexture2D<uint> occupancySliceMaskTexture = ResourceDescriptorHeap[config.occupancySliceMaskUAVDescriptorIndex];
    RWTexture2DArray<uint> extinctionTexture = ResourceDescriptorHeap[config.extinctionUAVDescriptorIndex];
    RWTexture2D<uint> zeroTransmittanceSliceTexture = ResourceDescriptorHeap[config.zeroTransmittanceSliceUAVDescriptorIndex];

    const uint2 lowPixel = dispatchThreadId.xy;
    if (occupancyTexture[lowPixel] <= 0.0f)
    {
        return;
    }

    const uint occupancySliceMask = occupancySliceMaskTexture[lowPixel];
    if (occupancySliceMask == 0u)
    {
        return;
    }

    zeroTransmittanceSliceTexture[lowPixel] = config.sliceCount;

    [loop]
    for (uint sliceIndex = 0u; sliceIndex < config.sliceCount; ++sliceIndex)
    {
        const uint sliceBit = 1u << sliceIndex;
        if ((occupancySliceMask & sliceBit) != 0u)
        {
            extinctionTexture[uint3(lowPixel, sliceIndex)] = 0u;
        }
    }
}