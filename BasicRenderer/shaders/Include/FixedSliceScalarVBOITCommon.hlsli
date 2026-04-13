#ifndef CLOD_FIXED_SLICE_SCALAR_VBOIT_COMMON_HLSLI
#define CLOD_FIXED_SLICE_SCALAR_VBOIT_COMMON_HLSLI

float ComputeBaseDepthSliceCoordinate(CLodFixedSliceScalarVBOITConfig config, float depth)
{
    const float depthRange = max(config.viewFarDepth - config.viewNearDepth, 1.0e-5f);
    const float normalizedDepth = saturate((depth - config.viewNearDepth) / depthRange);
    const float depthExponent = max(config.depthDistributionExponent, 1.0e-4f);
    const float distributedDepth = pow(normalizedDepth, depthExponent);
    const uint domainSliceCount = max(config.virtualSliceCount, max(config.sliceCount, 1u));
    return distributedDepth * (float)(domainSliceCount - 1u);
}

float ComputeDepthSliceCoordinate(CLodFixedSliceScalarVBOITConfig config, float depth)
{
    const float baseSliceCoordinate = ComputeBaseDepthSliceCoordinate(config, depth);
    if (config.depthWarpLUTSRVDescriptorIndex == 0xFFFFFFFFu || config.virtualSliceCount == 0u)
    {
        return baseSliceCoordinate;
    }

    StructuredBuffer<float> depthWarpLUT = ResourceDescriptorHeap[config.depthWarpLUTSRVDescriptorIndex];
    const uint sliceIndex0 = min((uint)floor(baseSliceCoordinate), config.virtualSliceCount - 1u);
    const uint sliceIndex1 = min(sliceIndex0 + 1u, config.virtualSliceCount - 1u);
    const float warpedSliceCoordinate0 = depthWarpLUT[sliceIndex0];
    const float warpedSliceCoordinate1 = depthWarpLUT[sliceIndex1];
    return lerp(warpedSliceCoordinate0, warpedSliceCoordinate1, saturate(baseSliceCoordinate - (float)sliceIndex0));
}

float ComputeLookupSliceCoordinate(CLodFixedSliceScalarVBOITConfig config, float sliceCoordinate)
{
    return clamp(
        sliceCoordinate - config.lookupDepthBiasInSlices,
        0.0f,
        (float)(max(config.sliceCount, 1u) - 1u));
}

#endif // CLOD_FIXED_SLICE_SCALAR_VBOIT_COMMON_HLSLI