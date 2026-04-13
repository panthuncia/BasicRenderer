#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/AVBOITCommon.hlsli"
#include "PerPassRootConstants/clodAVBOITIntegrateRootConstants.h"

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodAVBOITSparseClearCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<CLodAVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_INTEGRATE_CONFIG_DESCRIPTOR_INDEX];
    const CLodAVBOITConfig config = configBuffer[0];

    if (config.sliceCount == 0u ||
        dispatchThreadId.x >= config.lowResolutionWidth ||
        dispatchThreadId.y >= config.lowResolutionHeight ||
        config.occupancyUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.occupancySliceMaskUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.scalarExtinctionUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.chromaticExtinctionUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.zeroTransmittanceSliceUAVDescriptorIndex == 0xFFFFFFFFu)
    {
        return;
    }

    RWTexture2D<float> occupancyTexture = ResourceDescriptorHeap[config.occupancyUAVDescriptorIndex];
    RWTexture2D<uint> occupancySliceMaskTexture = ResourceDescriptorHeap[config.occupancySliceMaskUAVDescriptorIndex];
    RWTexture2DArray<uint> scalarExtinctionTexture = ResourceDescriptorHeap[config.scalarExtinctionUAVDescriptorIndex];
    RWTexture2DArray<uint> chromaticExtinctionTexture = ResourceDescriptorHeap[config.chromaticExtinctionUAVDescriptorIndex];
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
            scalarExtinctionTexture[uint3(lowPixel, sliceIndex)] = 0u;
            chromaticExtinctionTexture[uint3(lowPixel, GetChromaticExtinctionSliceIndex(config, sliceIndex, 0u))] = 0u;
            chromaticExtinctionTexture[uint3(lowPixel, GetChromaticExtinctionSliceIndex(config, sliceIndex, 1u))] = 0u;
            chromaticExtinctionTexture[uint3(lowPixel, GetChromaticExtinctionSliceIndex(config, sliceIndex, 2u))] = 0u;
        }
    }
}