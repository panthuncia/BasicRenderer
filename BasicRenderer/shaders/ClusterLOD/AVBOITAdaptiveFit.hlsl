#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "PerPassRootConstants/clodAVBOITAdaptiveFitRootConstants.h"

uint ComputeSmoothedTargetVirtualSliceCount(uint currentVirtualSliceCount, uint targetVirtualSliceCount, uint minVirtualSliceCount, uint maxVirtualSliceCount)
{
    currentVirtualSliceCount = clamp(currentVirtualSliceCount, minVirtualSliceCount, maxVirtualSliceCount);
    targetVirtualSliceCount = clamp(targetVirtualSliceCount, minVirtualSliceCount, maxVirtualSliceCount);

    if (targetVirtualSliceCount > currentVirtualSliceCount + 1u)
    {
        const uint step = max(1u, (targetVirtualSliceCount - currentVirtualSliceCount + 1u) / 2u);
        return min(maxVirtualSliceCount, currentVirtualSliceCount + step);
    }

    if (targetVirtualSliceCount + 1u < currentVirtualSliceCount)
    {
        const uint step = max(1u, (currentVirtualSliceCount - targetVirtualSliceCount + 1u) / 2u);
        return max(minVirtualSliceCount, currentVirtualSliceCount - step);
    }

    return currentVirtualSliceCount;
}

float ComputeTargetDepthDistributionExponent(float normalizedWeightedSliceMean)
{
    const float centeredMean = 0.5f - saturate(normalizedWeightedSliceMean);
    const float targetExponent = exp2(centeredMean * 1.5f);
    return clamp(
        targetExponent,
        CLOD_AVBOIT_VBOIT_MIN_DEPTH_DISTRIBUTION_EXPONENT,
        CLOD_AVBOIT_VBOIT_MAX_DEPTH_DISTRIBUTION_EXPONENT);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void CLodAVBOITAdaptiveFitCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u || dispatchThreadId.y != 0u || dispatchThreadId.z != 0u)
    {
        return;
    }

    RWStructuredBuffer<CLodAVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_ADAPTIVE_FIT_CONFIG_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodAVBOITFitState> fitStateBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_ADAPTIVE_FIT_STATE_DESCRIPTOR_INDEX];

    CLodAVBOITConfig config = configBuffer[0];
    const CLodAVBOITFitState fitState = fitStateBuffer[0];
    const uint minVirtualSliceCount = max(config.sliceCount, 1u);
    const uint maxVirtualSliceCount = CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT;
    uint fittedVirtualSliceCount = fitState.fittedVirtualSliceCount;
    if (fittedVirtualSliceCount < minVirtualSliceCount || fittedVirtualSliceCount > maxVirtualSliceCount)
    {
        fittedVirtualSliceCount = max(config.virtualSliceCount, minVirtualSliceCount);
    }

    config.virtualSliceCount = clamp(fittedVirtualSliceCount, minVirtualSliceCount, maxVirtualSliceCount);
    float fittedDepthDistributionExponent = fitState.fittedDepthDistributionExponent;
    if (fittedDepthDistributionExponent < CLOD_AVBOIT_VBOIT_MIN_DEPTH_DISTRIBUTION_EXPONENT ||
        fittedDepthDistributionExponent > CLOD_AVBOIT_VBOIT_MAX_DEPTH_DISTRIBUTION_EXPONENT)
    {
        fittedDepthDistributionExponent = config.depthDistributionExponent;
    }

    config.depthDistributionExponent = clamp(
        fittedDepthDistributionExponent,
        CLOD_AVBOIT_VBOIT_MIN_DEPTH_DISTRIBUTION_EXPONENT,
        CLOD_AVBOIT_VBOIT_MAX_DEPTH_DISTRIBUTION_EXPONENT);
    configBuffer[0] = config;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void CLodAVBOITAdaptiveFitUpdateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u || dispatchThreadId.y != 0u || dispatchThreadId.z != 0u)
    {
        return;
    }

    StructuredBuffer<CLodAVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_ADAPTIVE_FIT_CONFIG_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> occupancyHistogramBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_ADAPTIVE_FIT_HISTOGRAM_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodAVBOITFitState> fitStateBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_ADAPTIVE_FIT_STATE_DESCRIPTOR_INDEX];

    const CLodAVBOITConfig config = configBuffer[0];
    const uint minVirtualSliceCount = max(config.sliceCount, 1u);
    const uint maxVirtualSliceCount = CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT;
    uint occupiedVirtualSliceCount = 0u;
    uint totalOccupiedSamples = 0u;
    uint weightedSliceSum = 0u;
    [unroll(CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT)]
    for (uint sliceIndex = 0u; sliceIndex < CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT; ++sliceIndex)
    {
        if (sliceIndex >= config.virtualSliceCount)
        {
            break;
        }

        const uint histogramValue = occupancyHistogramBuffer[sliceIndex];
        occupiedVirtualSliceCount += histogramValue != 0u ? 1u : 0u;
        totalOccupiedSamples += histogramValue;
        weightedSliceSum += histogramValue * sliceIndex;
    }

    const uint currentVirtualSliceCount = clamp(config.virtualSliceCount, minVirtualSliceCount, maxVirtualSliceCount);
    uint targetVirtualSliceCount = currentVirtualSliceCount;
    if (occupiedVirtualSliceCount > 0u)
    {
        targetVirtualSliceCount = max(
            minVirtualSliceCount,
            (currentVirtualSliceCount * config.sliceCount + occupiedVirtualSliceCount - 1u) / occupiedVirtualSliceCount);
    }
    else
    {
        targetVirtualSliceCount = maxVirtualSliceCount;
    }

    const uint fittedVirtualSliceCount = ComputeSmoothedTargetVirtualSliceCount(
        currentVirtualSliceCount,
        targetVirtualSliceCount,
        minVirtualSliceCount,
        maxVirtualSliceCount);

    const float currentDepthDistributionExponent = clamp(
        config.depthDistributionExponent,
        CLOD_AVBOIT_VBOIT_MIN_DEPTH_DISTRIBUTION_EXPONENT,
        CLOD_AVBOIT_VBOIT_MAX_DEPTH_DISTRIBUTION_EXPONENT);
    float fittedDepthDistributionExponent = currentDepthDistributionExponent;
    if (totalOccupiedSamples > 0u && config.virtualSliceCount > 1u)
    {
        const float normalizedWeightedSliceMean =
            (float)weightedSliceSum /
            ((float)totalOccupiedSamples * (float)(config.virtualSliceCount - 1u));
        const float targetDepthDistributionExponent =
            ComputeTargetDepthDistributionExponent(normalizedWeightedSliceMean);
        fittedDepthDistributionExponent = lerp(
            currentDepthDistributionExponent,
            targetDepthDistributionExponent,
            0.35f);
    }

    CLodAVBOITFitState fitState;
    fitState.fittedVirtualSliceCount = clamp(fittedVirtualSliceCount, minVirtualSliceCount, maxVirtualSliceCount);
    fitState.occupiedVirtualSliceCount = occupiedVirtualSliceCount;
    fitState.fittedDepthDistributionExponent = clamp(
        fittedDepthDistributionExponent,
        CLOD_AVBOIT_VBOIT_MIN_DEPTH_DISTRIBUTION_EXPONENT,
        CLOD_AVBOIT_VBOIT_MAX_DEPTH_DISTRIBUTION_EXPONENT);
    fitState.pad1 = 0u;
    fitStateBuffer[0] = fitState;
}