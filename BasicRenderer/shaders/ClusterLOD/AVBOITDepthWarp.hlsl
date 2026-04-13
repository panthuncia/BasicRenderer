#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "PerPassRootConstants/clodAVBOITDepthWarpRootConstants.h"

float ComputeMappedRankCoordinate(uint rank, float rankScale)
{
    return (float)rank * rankScale;
}

bool IsVirtualSliceOccupied(StructuredBuffer<uint> occupancyHistogramBuffer, CLodAVBOITConfig config, uint sliceIndex)
{
    return sliceIndex < config.virtualSliceCount && occupancyHistogramBuffer[sliceIndex] != 0u;
}

uint CountOccupiedVirtualSlices(StructuredBuffer<uint> occupancyHistogramBuffer, CLodAVBOITConfig config)
{
    uint occupiedCount = 0u;
    [unroll(CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT)]
    for (uint sliceIndex = 0u; sliceIndex < CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT; ++sliceIndex)
    {
        if (sliceIndex >= config.virtualSliceCount)
        {
            break;
        }

        occupiedCount += occupancyHistogramBuffer[sliceIndex] != 0u ? 1u : 0u;
    }
    return occupiedCount;
}

uint GetOccupiedRankAtOrBefore(StructuredBuffer<uint> occupancyHistogramBuffer, CLodAVBOITConfig config, uint sliceIndex, out bool found)
{
    found = false;
    uint occupiedRank = 0u;
    uint previousOccupiedRank = 0u;
    [unroll(CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT)]
    for (uint prefixIndex = 0u; prefixIndex < CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT; ++prefixIndex)
    {
        if (prefixIndex >= config.virtualSliceCount)
        {
            break;
        }

        if (occupancyHistogramBuffer[prefixIndex] == 0u)
        {
            continue;
        }

        if (prefixIndex <= sliceIndex)
        {
            previousOccupiedRank = occupiedRank;
            found = true;
        }
        occupiedRank++;
    }

    return previousOccupiedRank;
}

uint GetOccupiedRankAtOrAfter(StructuredBuffer<uint> occupancyHistogramBuffer, CLodAVBOITConfig config, uint sliceIndex, out bool found)
{
    found = false;
    uint occupiedRank = 0u;
    [unroll(CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT)]
    for (uint prefixIndex = 0u; prefixIndex < CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT; ++prefixIndex)
    {
        if (prefixIndex >= config.virtualSliceCount)
        {
            break;
        }

        if (occupancyHistogramBuffer[prefixIndex] == 0u)
        {
            continue;
        }

        if (prefixIndex >= sliceIndex)
        {
            found = true;
            return occupiedRank;
        }

        occupiedRank++;
    }

    return 0u;
}

uint GetNextOccupiedSliceIndex(StructuredBuffer<uint> occupancyHistogramBuffer, CLodAVBOITConfig config, uint sliceIndex, out bool found)
{
    found = false;
    [unroll(CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT)]
    for (uint prefixIndex = 0u; prefixIndex < CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT; ++prefixIndex)
    {
        if (prefixIndex >= config.virtualSliceCount)
        {
            break;
        }

        if (prefixIndex >= sliceIndex && occupancyHistogramBuffer[prefixIndex] != 0u)
        {
            found = true;
            return prefixIndex;
        }
    }

    return 0u;
}

uint GetPreviousOccupiedSliceIndex(StructuredBuffer<uint> occupancyHistogramBuffer, CLodAVBOITConfig config, uint sliceIndex, out bool found)
{
    found = false;
    uint previousSliceIndex = 0u;
    [unroll(CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT)]
    for (uint prefixIndex = 0u; prefixIndex < CLOD_AVBOIT_VBOIT_DEFAULT_VIRTUAL_SLICE_COUNT; ++prefixIndex)
    {
        if (prefixIndex >= config.virtualSliceCount)
        {
            break;
        }

        if (prefixIndex <= sliceIndex && occupancyHistogramBuffer[prefixIndex] != 0u)
        {
            previousSliceIndex = prefixIndex;
            found = true;
        }
    }

    return previousSliceIndex;
}

float ComputeLinearFallbackWarpedCoordinate(CLodAVBOITConfig config, float baseSliceCoordinate)
{
    const uint sourceDomainSliceCount = max(config.virtualSliceCount, max(config.sliceCount, 1u));
    if (sourceDomainSliceCount <= 1u || config.sliceCount <= 1u)
    {
        return 0.0f;
    }

    return saturate(baseSliceCoordinate / (float)(sourceDomainSliceCount - 1u)) *
        (float)(config.sliceCount - 1u);
}

float ComputeWarpedSliceCoordinateFromHistogram(
    StructuredBuffer<uint> occupancyHistogramBuffer,
    CLodAVBOITConfig config,
    float baseSliceCoordinate)
{
    const float clampedBaseSliceCoordinate = clamp(
        baseSliceCoordinate,
        0.0f,
        (float)(max(config.virtualSliceCount, 1u) - 1u));
    const uint occupiedCount = CountOccupiedVirtualSlices(occupancyHistogramBuffer, config);
    if (occupiedCount < 2u)
    {
        return ComputeLinearFallbackWarpedCoordinate(config, clampedBaseSliceCoordinate);
    }

    const float rankScale = (float)(config.sliceCount - 1u) / (float)(occupiedCount - 1u);
    const uint lowerSliceIndex = min((uint)floor(clampedBaseSliceCoordinate), config.virtualSliceCount - 1u);
    const uint upperSliceIndex = min(lowerSliceIndex + 1u, config.virtualSliceCount - 1u);
    const bool lowerOccupied = IsVirtualSliceOccupied(occupancyHistogramBuffer, config, lowerSliceIndex);
    const bool upperOccupied = IsVirtualSliceOccupied(occupancyHistogramBuffer, config, upperSliceIndex);

    bool foundPreviousRank = false;
    bool foundNextRank = false;
    const uint previousOccupiedRank = GetOccupiedRankAtOrBefore(
        occupancyHistogramBuffer,
        config,
        lowerSliceIndex,
        foundPreviousRank);
    const uint nextOccupiedRank = GetOccupiedRankAtOrAfter(
        occupancyHistogramBuffer,
        config,
        upperSliceIndex,
        foundNextRank);

    bool foundPreviousSlice = false;
    bool foundNextSlice = false;
    const uint previousOccupiedSliceIndex = GetPreviousOccupiedSliceIndex(
        occupancyHistogramBuffer,
        config,
        lowerSliceIndex,
        foundPreviousSlice);
    const uint nextOccupiedSliceIndex = GetNextOccupiedSliceIndex(
        occupancyHistogramBuffer,
        config,
        upperSliceIndex,
        foundNextSlice);

    if (lowerOccupied && upperOccupied)
    {
        const float interpolationFactor = saturate(clampedBaseSliceCoordinate - (float)lowerSliceIndex);
        return lerp(
            ComputeMappedRankCoordinate(previousOccupiedRank, rankScale),
            ComputeMappedRankCoordinate(nextOccupiedRank, rankScale),
            interpolationFactor);
    }

    if (foundPreviousSlice && foundNextSlice && nextOccupiedSliceIndex == previousOccupiedSliceIndex + 1u)
    {
        const float segmentExtent = max(
            (float)(nextOccupiedSliceIndex - previousOccupiedSliceIndex),
            1.0e-5f);
        const float interpolationFactor = saturate(
            (clampedBaseSliceCoordinate - (float)previousOccupiedSliceIndex) / segmentExtent);
        return lerp(
            ComputeMappedRankCoordinate(previousOccupiedRank, rankScale),
            ComputeMappedRankCoordinate(nextOccupiedRank, rankScale),
            interpolationFactor);
    }

    if (foundPreviousSlice && foundNextSlice)
    {
        const float boundaryCoordinate = 0.5f * ((float)previousOccupiedSliceIndex + (float)nextOccupiedSliceIndex);
        return clampedBaseSliceCoordinate < boundaryCoordinate
            ? ComputeMappedRankCoordinate(previousOccupiedRank, rankScale)
            : ComputeMappedRankCoordinate(nextOccupiedRank, rankScale);
    }

    if (foundPreviousRank)
    {
        return ComputeMappedRankCoordinate(previousOccupiedRank, rankScale);
    }

    if (foundNextRank)
    {
        return ComputeMappedRankCoordinate(nextOccupiedRank, rankScale);
    }

    return ComputeLinearFallbackWarpedCoordinate(config, clampedBaseSliceCoordinate);
}

bool ShouldFilterToNextLUTSample(
    StructuredBuffer<uint> occupancyHistogramBuffer,
    CLodAVBOITConfig config,
    float baseSliceCoordinate0,
    float baseSliceCoordinate1)
{
    const float midpoint = 0.5f * (baseSliceCoordinate0 + baseSliceCoordinate1);
    const uint lowerSliceIndex = min((uint)floor(midpoint), config.virtualSliceCount - 1u);
    const uint upperSliceIndex = min(lowerSliceIndex + 1u, config.virtualSliceCount - 1u);
    return IsVirtualSliceOccupied(occupancyHistogramBuffer, config, lowerSliceIndex) &&
        IsVirtualSliceOccupied(occupancyHistogramBuffer, config, upperSliceIndex);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void CLodAVBOITDepthWarpCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<CLodAVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_DEPTH_WARP_CONFIG_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> occupancyHistogramBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_DEPTH_WARP_HISTOGRAM_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodAVBOITDepthWarpLUTEntry> depthWarpLUTBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_DEPTH_WARP_LUT_DESCRIPTOR_INDEX];
    const CLodAVBOITConfig config = configBuffer[0];
    const uint lutIndex = dispatchThreadId.x;

    if (lutIndex >= CLOD_AVBOIT_VBOIT_DEPTH_WARP_LUT_RESOLUTION)
    {
        return;
    }

    CLodAVBOITDepthWarpLUTEntry depthWarpEntry;
    const float baseSliceCoordinate = config.virtualSliceCount <= 1u
        ? 0.0f
        : ((float)lutIndex / (float)(CLOD_AVBOIT_VBOIT_DEPTH_WARP_LUT_RESOLUTION - 1u)) *
            (float)(config.virtualSliceCount - 1u);
    depthWarpEntry.warpedSliceCoordinate = ComputeWarpedSliceCoordinateFromHistogram(
        occupancyHistogramBuffer,
        config,
        baseSliceCoordinate);
    if (lutIndex + 1u < CLOD_AVBOIT_VBOIT_DEPTH_WARP_LUT_RESOLUTION)
    {
        const float nextBaseSliceCoordinate = config.virtualSliceCount <= 1u
            ? 0.0f
            : ((float)(lutIndex + 1u) / (float)(CLOD_AVBOIT_VBOIT_DEPTH_WARP_LUT_RESOLUTION - 1u)) *
                (float)(config.virtualSliceCount - 1u);
        depthWarpEntry.flags = ShouldFilterToNextLUTSample(
            occupancyHistogramBuffer,
            config,
            baseSliceCoordinate,
            nextBaseSliceCoordinate)
        ? CLOD_AVBOIT_VBOIT_DEPTH_WARP_FLAG_FILTER_TO_NEXT
            : 0u;
    }
    else
    {
        depthWarpEntry.flags = 0u;
    }
    depthWarpLUTBuffer[lutIndex] = depthWarpEntry;
}