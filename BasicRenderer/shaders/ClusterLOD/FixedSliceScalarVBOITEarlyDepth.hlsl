#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"
#include "include/FixedSliceScalarVBOITCommon.hlsli"
#include "fullscreenVS.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITEarlyDepthRootConstants.h"

float ComputeDepthFromBaseSliceCoordinate(CLodFixedSliceScalarVBOITConfig config, float baseSliceCoordinate)
{
    const uint domainSliceCount = max(config.virtualSliceCount, max(config.sliceCount, 1u));
    if (domainSliceCount <= 1u)
    {
        return config.viewNearDepth;
    }

    const float normalizedDistributedDepth = saturate(baseSliceCoordinate / (float)(domainSliceCount - 1u));
    const float inverseExponent = rcp(max(config.depthDistributionExponent, 1.0e-4f));
    const float normalizedDepth = pow(normalizedDistributedDepth, inverseExponent);
    return lerp(config.viewNearDepth, config.viewFarDepth, normalizedDepth);
}

float InvertWarpedSliceCoordinate(CLodFixedSliceScalarVBOITConfig config, float warpedSliceCoordinate)
{
    if (config.sliceCount <= 1u)
    {
        return 0.0f;
    }

    const float clampedWarpedSliceCoordinate = clamp(
        warpedSliceCoordinate,
        0.0f,
        (float)(config.sliceCount - 1u));

    if (config.depthWarpLUTSRVDescriptorIndex == 0xFFFFFFFFu || config.virtualSliceCount == 0u)
    {
        return clampedWarpedSliceCoordinate *
            ((float)(max(config.virtualSliceCount, max(config.sliceCount, 1u)) - 1u) / (float)(config.sliceCount - 1u));
    }

    StructuredBuffer<CLodFixedSliceScalarVBOITDepthWarpLUTEntry> depthWarpLUT =
        ResourceDescriptorHeap[config.depthWarpLUTSRVDescriptorIndex];

    [loop]
    for (uint virtualSliceIndex = 0u; virtualSliceIndex < config.virtualSliceCount; ++virtualSliceIndex)
    {
        const CLodFixedSliceScalarVBOITDepthWarpLUTEntry depthWarpEntry0 = depthWarpLUT[virtualSliceIndex];
        if (depthWarpEntry0.warpedSliceCoordinate >= clampedWarpedSliceCoordinate)
        {
            return (float)virtualSliceIndex;
        }

        if (virtualSliceIndex + 1u >= config.virtualSliceCount ||
            (depthWarpEntry0.flags & CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_FLAG_FILTER_TO_NEXT) == 0u)
        {
            continue;
        }

        const CLodFixedSliceScalarVBOITDepthWarpLUTEntry depthWarpEntry1 = depthWarpLUT[virtualSliceIndex + 1u];
        if (depthWarpEntry1.warpedSliceCoordinate < clampedWarpedSliceCoordinate)
        {
            continue;
        }

        const float segmentExtent = max(
            depthWarpEntry1.warpedSliceCoordinate - depthWarpEntry0.warpedSliceCoordinate,
            1.0e-5f);
        const float interpolationFactor = saturate(
            (clampedWarpedSliceCoordinate - depthWarpEntry0.warpedSliceCoordinate) / segmentExtent);
        return lerp((float)virtualSliceIndex, (float)(virtualSliceIndex + 1u), interpolationFactor);
    }

    return (float)(config.virtualSliceCount - 1u);
}

[shader("pixel")]
float FixedSliceScalarVBOITEarlyDepthPSMain(FULLSCREEN_VS_OUTPUT input) : SV_Depth
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)];
    StructuredBuffer<Camera> cameraBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_EARLY_DEPTH_CONFIG_DESCRIPTOR_INDEX];
    Texture2D<uint> zeroTransmittanceSliceTexture =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_EARLY_DEPTH_ZERO_SLICE_DESCRIPTOR_INDEX];

    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];
    const uint2 pixel = uint2(input.position.xy);
    if (config.sliceCount == 0u ||
        pixel.x >= perFrameBuffer.screenResX ||
        pixel.y >= perFrameBuffer.screenResY)
    {
        discard;
    }

    const uint2 lowPixel = pixel / CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR;
    if (lowPixel.x >= config.lowResolutionWidth || lowPixel.y >= config.lowResolutionHeight)
    {
        discard;
    }

    const uint zeroTransmittanceSlice = zeroTransmittanceSliceTexture[lowPixel];
    if (zeroTransmittanceSlice >= config.sliceCount)
    {
        discard;
    }

    const float baseSliceCoordinate = InvertWarpedSliceCoordinate(config, (float)zeroTransmittanceSlice);
    const float linearDepth = max(ComputeDepthFromBaseSliceCoordinate(config, baseSliceCoordinate), config.viewNearDepth);
    const Camera mainCamera = cameraBuffer[perFrameBuffer.mainCameraIndex];
    const float projectedDepth = -mainCamera.projection[2][2] + mainCamera.projection[3][2] / linearDepth;
    return saturate(projectedDepth);
}