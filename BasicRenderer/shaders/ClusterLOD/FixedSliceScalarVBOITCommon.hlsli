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

    StructuredBuffer<CLodFixedSliceScalarVBOITDepthWarpLUTEntry> depthWarpLUT =
        ResourceDescriptorHeap[config.depthWarpLUTSRVDescriptorIndex];
    const uint sliceIndex0 = min((uint)floor(baseSliceCoordinate), config.virtualSliceCount - 1u);
    const uint sliceIndex1 = min(sliceIndex0 + 1u, config.virtualSliceCount - 1u);
    const CLodFixedSliceScalarVBOITDepthWarpLUTEntry depthWarpEntry0 = depthWarpLUT[sliceIndex0];
    if (sliceIndex0 == sliceIndex1 ||
        (depthWarpEntry0.flags & CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_FLAG_FILTER_TO_NEXT) == 0u)
    {
        return depthWarpEntry0.warpedSliceCoordinate;
    }

    const CLodFixedSliceScalarVBOITDepthWarpLUTEntry depthWarpEntry1 = depthWarpLUT[sliceIndex1];
    return lerp(
        depthWarpEntry0.warpedSliceCoordinate,
        depthWarpEntry1.warpedSliceCoordinate,
        saturate(baseSliceCoordinate - (float)sliceIndex0));
}

float ComputeLookupSliceCoordinate(CLodFixedSliceScalarVBOITConfig config, float sliceCoordinate)
{
    return clamp(
        sliceCoordinate - config.lookupDepthBiasInSlices,
        0.0f,
        (float)(max(config.sliceCount, 1u) - 1u));
}

float SampleIntegratedTransmittanceAtSliceCoordinate(
    Texture2DArray<float> integratedTransmittanceTexture,
    CLodFixedSliceScalarVBOITConfig config,
    PerFrameBuffer perFrameBuffer,
    uint2 pixel,
    float sliceCoordinate)
{
    if (config.sliceCount == 0u ||
        config.lowResolutionWidth == 0u ||
        config.lowResolutionHeight == 0u ||
        perFrameBuffer.screenResX == 0u ||
        perFrameBuffer.screenResY == 0u)
    {
        return 1.0f;
    }

    const float2 uv = (float2(pixel) + 0.5f) /
        float2((float)perFrameBuffer.screenResX, (float)perFrameBuffer.screenResY);
    const float lookupSliceCoordinate = ComputeLookupSliceCoordinate(config, sliceCoordinate);
    const uint sliceIndex = min((uint)floor(lookupSliceCoordinate), config.sliceCount - 1u);
    const float sliceLerpFactor = saturate(lookupSliceCoordinate - (float)sliceIndex);
    const float previousTransmittance = sliceIndex == 0u
        ? 1.0f
        : integratedTransmittanceTexture.SampleLevel(g_linearClamp, float3(uv, (float)(sliceIndex - 1u)), 0.0f);
    const float currentTransmittance = integratedTransmittanceTexture.SampleLevel(
        g_linearClamp,
        float3(uv, (float)sliceIndex),
        0.0f);
    return lerp(previousTransmittance, currentTransmittance, sliceLerpFactor);
}

#endif // CLOD_FIXED_SLICE_SCALAR_VBOIT_COMMON_HLSLI