#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/outputTypes.hlsli"
#include "include/debugPayload.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITIntegrateRootConstants.h"

bool IsFixedSliceScalarVBOITDebugOutput(uint outputType)
{
    return outputType == OUTPUT_TRANSPARENT_VBOIT_TRANSMITTANCE ||
        outputType == OUTPUT_TRANSPARENT_VBOIT_COVERAGE ||
        outputType == OUTPUT_TRANSPARENT_VBOIT_ZERO_SLICE ||
        outputType == OUTPUT_TRANSPARENT_VBOIT_VIRTUAL_SLICE_COUNT ||
        outputType == OUTPUT_TRANSPARENT_VBOIT_PHYSICAL_SLICE_COUNT ||
        outputType == OUTPUT_TRANSPARENT_VBOIT_FITTED_VIRTUAL_SLICE_COUNT ||
        outputType == OUTPUT_TRANSPARENT_VBOIT_OCCUPIED_VIRTUAL_SLICE_COUNT ||
        outputType == OUTPUT_TRANSPARENT_VBOIT_DEPTH_DISTRIBUTION_EXPONENT;
}

float GetNormalizedDepthDistributionExponent(CLodFixedSliceScalarVBOITConfig config)
{
    const float exponentRange =
        CLOD_FIXED_SLICE_SCALAR_VBOIT_MAX_DEPTH_DISTRIBUTION_EXPONENT -
        CLOD_FIXED_SLICE_SCALAR_VBOIT_MIN_DEPTH_DISTRIBUTION_EXPONENT;
    if (exponentRange <= 1.0e-5f)
    {
        return 0.0f;
    }

    return saturate(
        (config.depthDistributionExponent - CLOD_FIXED_SLICE_SCALAR_VBOIT_MIN_DEPTH_DISTRIBUTION_EXPONENT) /
        exponentRange);
}

uint2 GetFixedSliceScalarVBOITDebugPayload(
    uint outputType,
    CLodFixedSliceScalarVBOITConfig config,
    CLodFixedSliceScalarVBOITFitState fitState,
    float transmittance,
    float coverage,
    uint zeroTransmittanceSlice,
    uint occupiedPhysicalSliceCount)
{
    switch (outputType)
    {
        case OUTPUT_TRANSPARENT_VBOIT_TRANSMITTANCE:
            return PackDebugFloat1(transmittance);
        case OUTPUT_TRANSPARENT_VBOIT_COVERAGE:
            return PackDebugFloat1(saturate(coverage));
        case OUTPUT_TRANSPARENT_VBOIT_ZERO_SLICE:
        {
            const float normalizedZeroSlice = zeroTransmittanceSlice >= config.sliceCount || config.sliceCount <= 1u
                ? 1.0f
                : (float)zeroTransmittanceSlice / (float)(config.sliceCount - 1u);
            return PackDebugFloat1(normalizedZeroSlice);
        }
        case OUTPUT_TRANSPARENT_VBOIT_VIRTUAL_SLICE_COUNT:
            return PackDebugFloat1((float)config.virtualSliceCount / (float)CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT);
        case OUTPUT_TRANSPARENT_VBOIT_PHYSICAL_SLICE_COUNT:
            return PackDebugFloat1((float)occupiedPhysicalSliceCount / (float)max(config.sliceCount, 1u));
        case OUTPUT_TRANSPARENT_VBOIT_FITTED_VIRTUAL_SLICE_COUNT:
            return PackDebugFloat1((float)fitState.fittedVirtualSliceCount / (float)CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT);
        case OUTPUT_TRANSPARENT_VBOIT_OCCUPIED_VIRTUAL_SLICE_COUNT:
            return PackDebugFloat1((float)fitState.occupiedVirtualSliceCount / (float)CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT);
        case OUTPUT_TRANSPARENT_VBOIT_DEPTH_DISTRIBUTION_EXPONENT:
            return PackDebugFloat1(GetNormalizedDepthDistributionExponent(config));
        default:
            return uint2(DEBUG_SENTINEL, DEBUG_SENTINEL);
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodFixedSliceScalarVBOITIntegrateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)];
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_CONFIG_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodFixedSliceScalarVBOITFitState> fitStateBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_FIT_STATE_DESCRIPTOR_INDEX];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];
    const CLodFixedSliceScalarVBOITFitState fitState = fitStateBuffer[0];

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
    const uint occupiedPhysicalSliceCount = countbits(lowTileSliceMask);
    uint zeroTransmittanceSlice = zeroTransmittanceSliceTexture[lowPixel];

    if (!lowTileOccupied || lowTileSliceMask == 0u)
    {
        if (IsFixedSliceScalarVBOITDebugOutput(perFrameBuffer.outputType))
        {
            RWTexture2D<uint2> debugVisTex = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::DebugVisualization)];
            const uint2 payload = GetFixedSliceScalarVBOITDebugPayload(
                perFrameBuffer.outputType,
                config,
                fitState,
                1.0f,
                0.0f,
                config.sliceCount,
                0u);
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
            zeroTransmittanceSlice = sliceIndex;
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
        const uint2 payload = GetFixedSliceScalarVBOITDebugPayload(
            perFrameBuffer.outputType,
            config,
            fitState,
            cumulativeTransmittance,
            coverage,
            zeroTransmittanceSlice,
            occupiedPhysicalSliceCount);
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