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

float ComputeHardwareDepthFromLinearDepth(float linearDepth, Camera camera)
{
    const float nearPlane = max(camera.zNear, 1.0e-5f);
    const float farPlane = max(camera.zFar, nearPlane + 1.0e-5f);
    const float clampedLinearDepth = clamp(linearDepth, nearPlane, farPlane);
    return saturate((farPlane - (nearPlane * farPlane) / clampedLinearDepth) / (farPlane - nearPlane));
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

    uint lowerLUTIndex = 0u;
    uint upperLUTIndex = CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_LUT_RESOLUTION - 1u;
    [unroll(13)]
    for (uint iterationIndex = 0u; iterationIndex < 13u; ++iterationIndex)
    {
        const uint midLUTIndex = (lowerLUTIndex + upperLUTIndex) >> 1u;
        const CLodFixedSliceScalarVBOITDepthWarpLUTEntry midDepthWarpEntry = depthWarpLUT[midLUTIndex];
        if (midDepthWarpEntry.warpedSliceCoordinate < clampedWarpedSliceCoordinate)
        {
            lowerLUTIndex = min(midLUTIndex + 1u, CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_LUT_RESOLUTION - 1u);
        }
        else
        {
            upperLUTIndex = midLUTIndex;
        }
    }

    const uint lutIndex1 = min(upperLUTIndex, CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_LUT_RESOLUTION - 1u);
    if (lutIndex1 == 0u)
    {
        return ComputeBaseSliceCoordinateFromDepthWarpLUTSampleCoordinate(config, 0.0f);
    }

    const uint lutIndex0 = lutIndex1 - 1u;
    const CLodFixedSliceScalarVBOITDepthWarpLUTEntry depthWarpEntry0 = depthWarpLUT[lutIndex0];
    if ((depthWarpEntry0.flags & CLOD_FIXED_SLICE_SCALAR_VBOIT_DEPTH_WARP_FLAG_FILTER_TO_NEXT) == 0u)
    {
        return ComputeBaseSliceCoordinateFromDepthWarpLUTSampleCoordinate(config, (float)lutIndex1);
    }

    const CLodFixedSliceScalarVBOITDepthWarpLUTEntry depthWarpEntry1 = depthWarpLUT[lutIndex1];
    const float segmentExtent = max(
        depthWarpEntry1.warpedSliceCoordinate - depthWarpEntry0.warpedSliceCoordinate,
        1.0e-5f);
    const float interpolationFactor = saturate(
        (clampedWarpedSliceCoordinate - depthWarpEntry0.warpedSliceCoordinate) / segmentExtent);
    return ComputeBaseSliceCoordinateFromDepthWarpLUTSampleCoordinate(
        config,
        lerp((float)lutIndex0, (float)lutIndex1, interpolationFactor));
}

float ComputeConservativeEarlyDepthWarpedSliceCoordinate(
    CLodFixedSliceScalarVBOITConfig config,
    uint zeroTransmittanceSlice)
{
    const float guaranteedZeroLookupSliceCoordinate =
        (float)zeroTransmittanceSlice + 1.0f;
    return guaranteedZeroLookupSliceCoordinate + max(config.lookupDepthBiasInSlices, 0.0f);
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

    const float conservativeWarpedSliceCoordinate =
        ComputeConservativeEarlyDepthWarpedSliceCoordinate(config, zeroTransmittanceSlice);
    if (conservativeWarpedSliceCoordinate > (float)(config.sliceCount - 1u))
    {
        discard;
    }

    const float baseSliceCoordinate = InvertWarpedSliceCoordinate(config, conservativeWarpedSliceCoordinate);
    const float linearDepth = max(ComputeDepthFromBaseSliceCoordinate(config, baseSliceCoordinate), config.viewNearDepth);
    const Camera mainCamera = cameraBuffer[perFrameBuffer.mainCameraIndex];
    return ComputeHardwareDepthFromLinearDepth(linearDepth, mainCamera);
}