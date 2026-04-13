#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/outputTypes.hlsli"
#include "include/debugPayload.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITIntegrateRootConstants.h"

bool IsFixedSliceScalarVBOITDebugOutput(uint outputType)
{
    return outputType == OUTPUT_TRANSPARENT_VBOIT_TRANSMITTANCE ||
        outputType == OUTPUT_TRANSPARENT_VBOIT_COVERAGE;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodFixedSliceScalarVBOITIntegrateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)];
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_CONFIG_DESCRIPTOR_INDEX];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];

    if (config.sliceCount == 0u ||
        dispatchThreadId.x >= config.lowResolutionWidth ||
        dispatchThreadId.y >= config.lowResolutionHeight ||
        config.occupancyUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.coverageUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.occupancySliceMaskUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.extinctionUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.integratedTransmittanceUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.zeroTransmittanceSliceUAVDescriptorIndex == 0xFFFFFFFFu)
    {
        return;
    }

    RWTexture2DArray<uint> extinctionTexture = ResourceDescriptorHeap[config.extinctionUAVDescriptorIndex];
    RWTexture2D<float> occupancyTexture = ResourceDescriptorHeap[config.occupancyUAVDescriptorIndex];
    RWTexture2D<float> coverageTexture = ResourceDescriptorHeap[config.coverageUAVDescriptorIndex];
    RWTexture2D<uint> occupancySliceMaskTexture = ResourceDescriptorHeap[config.occupancySliceMaskUAVDescriptorIndex];
    RWTexture2DArray<float> integratedTransmittanceTexture = ResourceDescriptorHeap[config.integratedTransmittanceUAVDescriptorIndex];
    RWTexture2D<uint> zeroTransmittanceSliceTexture = ResourceDescriptorHeap[config.zeroTransmittanceSliceUAVDescriptorIndex];
    const uint2 lowPixel = dispatchThreadId.xy;
    const bool lowTileOccupied = occupancyTexture[lowPixel] > 0.0f;
    const uint lowTileSliceMask = occupancySliceMaskTexture[lowPixel];

    if (!lowTileOccupied || lowTileSliceMask == 0u)
    {
        if (IsFixedSliceScalarVBOITDebugOutput(perFrameBuffer.outputType))
        {
            RWTexture2D<uint2> debugVisTex = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::DebugVisualization)];
            const uint2 payload = perFrameBuffer.outputType == OUTPUT_TRANSPARENT_VBOIT_TRANSMITTANCE
                ? PackDebugFloat1(1.0f)
                : PackDebugUint(0u);
            const uint2 renderResolution = uint2(perFrameBuffer.screenResX, perFrameBuffer.screenResY);
            const uint2 tileMin = lowPixel * CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR;
            const uint2 tileMax = min(
                tileMin + uint2(CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR, CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR),
                renderResolution);
            [loop]
            for (uint fullY = tileMin.y; fullY < tileMax.y; ++fullY)
            {
                [loop]
                for (uint fullX = tileMin.x; fullX < tileMax.x; ++fullX)
                {
                    WriteDebugPixel(debugVisTex, uint2(fullX, fullY), payload);
                }
            }
        }
        return;
    }

    const float zeroTransmittanceThreshold = clamp(config.zeroTransmittanceThreshold, 0.0f, 1.0f);
    const uint firstOccupiedSlice = (uint)firstbitlow(lowTileSliceMask);
    const uint lastOccupiedSlice = (uint)firstbithigh(lowTileSliceMask);
    float cumulativeTransmittance = 1.0f;
    [loop]
    for (uint leadingSliceIndex = 0u; leadingSliceIndex < firstOccupiedSlice; ++leadingSliceIndex)
    {
        integratedTransmittanceTexture[uint3(lowPixel, leadingSliceIndex)] = cumulativeTransmittance;
    }

    [loop]
    for (uint sliceIndex = firstOccupiedSlice; sliceIndex <= lastOccupiedSlice; ++sliceIndex)
    {
        const uint sliceBit = 1u << sliceIndex;
        if ((lowTileSliceMask & sliceBit) == 0u)
        {
            integratedTransmittanceTexture[uint3(lowPixel, sliceIndex)] = cumulativeTransmittance;
            continue;
        }

        const float opticalDepth = (float)extinctionTexture[uint3(lowPixel, sliceIndex)] /
            CLOD_FIXED_SLICE_SCALAR_VBOIT_EXTINCTION_QUANTIZATION_SCALE;
        const float sliceExtinction = 1.0f - exp(-opticalDepth);
        cumulativeTransmittance *= (1.0f - sliceExtinction);

        if (zeroTransmittanceThreshold > 0.0f && cumulativeTransmittance <= zeroTransmittanceThreshold)
        {
            cumulativeTransmittance = 0.0f;
            zeroTransmittanceSliceTexture[lowPixel] = sliceIndex;
            integratedTransmittanceTexture[uint3(lowPixel, sliceIndex)] = 0.0f;
            [loop]
            for (uint remainingSliceIndex = sliceIndex + 1u; remainingSliceIndex < config.sliceCount; ++remainingSliceIndex)
            {
                integratedTransmittanceTexture[uint3(lowPixel, remainingSliceIndex)] = 0.0f;
            }
            break;
        }

        integratedTransmittanceTexture[uint3(lowPixel, sliceIndex)] = cumulativeTransmittance;
    }

    [loop]
    for (uint trailingSliceIndex = lastOccupiedSlice + 1u; trailingSliceIndex < config.sliceCount; ++trailingSliceIndex)
    {
        integratedTransmittanceTexture[uint3(lowPixel, trailingSliceIndex)] = cumulativeTransmittance;
    }

    coverageTexture[lowPixel] = 1.0f - cumulativeTransmittance;

    if (IsFixedSliceScalarVBOITDebugOutput(perFrameBuffer.outputType))
    {
        const float coverage = 1.0f - cumulativeTransmittance;
        RWTexture2D<uint2> debugVisTex = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::DebugVisualization)];
        const uint2 payload = perFrameBuffer.outputType == OUTPUT_TRANSPARENT_VBOIT_TRANSMITTANCE
            ? PackDebugFloat1(cumulativeTransmittance)
            : PackDebugUint(coverage > 1.0e-4f ? 1u : 0u);
        const uint2 renderResolution = uint2(perFrameBuffer.screenResX, perFrameBuffer.screenResY);
        const uint2 tileMin = lowPixel * CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR;
        const uint2 tileMax = min(
            tileMin + uint2(CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR, CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR),
            renderResolution);
        [loop]
        for (uint fullY = tileMin.y; fullY < tileMax.y; ++fullY)
        {
            [loop]
            for (uint fullX = tileMin.x; fullX < tileMax.x; ++fullX)
            {
                WriteDebugPixel(debugVisTex, uint2(fullX, fullY), payload);
            }
        }
    }
}