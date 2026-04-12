#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"
#include "include/visibilityPacking.hlsli"
#include "include/clodResolveCommon.hlsli"
#include "PerPassRootConstants/clodRasterizationRootConstants.h"

uint ComputeDepthSlice(CLodFixedSliceScalarVBOITConfig config, float depth)
{
    const float depthRange = max(config.viewFarDepth - config.viewNearDepth, 1.0e-5f);
    const float normalizedDepth = saturate((depth - config.viewNearDepth) / depthRange);
    return min(config.sliceCount - 1u, (uint)(normalizedDepth * config.sliceCount));
}

[shader("pixel")]
void FixedSliceScalarVBOITCapturePSMain(VisBufferPSInput input, bool isFrontFace : SV_IsFrontFace, uint primID : SV_PrimitiveID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)];
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer = ResourceDescriptorHeap[CLOD_RASTER_FIXED_SLICE_SCALAR_VBOIT_CONFIG_DESCRIPTOR_INDEX];

    const ClodViewRasterInfo viewRasterInfo = viewRasterInfoBuffer[input.viewID];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];
    if (viewRasterInfo.opaqueVisibilitySRVDescriptorIndex == 0xFFFFFFFFu ||
        config.sliceCount == 0u ||
        config.extinctionUAVDescriptorIndex == 0xFFFFFFFFu)
    {
        return;
    }

    const uint2 pixel = input.position.xy;
    if (pixel.x < viewRasterInfo.scissorMinX ||
        pixel.y < viewRasterInfo.scissorMinY ||
        pixel.x >= viewRasterInfo.scissorMaxX ||
        pixel.y >= viewRasterInfo.scissorMaxY)
    {
        return;
    }

#if defined(PSO_ALPHA_TEST)
    TestAlpha(input.texcoord, input.materialDataIndex);
#endif

    Texture2D<uint64_t> opaqueVisibility =
        ResourceDescriptorHeap[NonUniformResourceIndex(viewRasterInfo.opaqueVisibilitySRVDescriptorIndex)];
    const uint64_t opaqueVisKey = opaqueVisibility[pixel];
    if (opaqueVisKey != 0xFFFFFFFFFFFFFFFFu)
    {
        float opaqueDepth;
        uint opaqueClusterIndex;
        uint opaquePrimID;
        UnpackVisKey(opaqueVisKey, opaqueDepth, opaqueClusterIndex, opaquePrimID);
        if (input.linearDepth >= opaqueDepth)
        {
            return;
        }
    }

    ClodResolvedSample sample;
    if (!ResolveClodSampleFromVisKeyWithFace(PackVisKey(input.linearDepth, input.visibleClusterIndex, primID), pixel, !isFrontFace, sample))
    {
        return;
    }

    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    const Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];

    FragmentInfo fragmentInfo = (FragmentInfo)0;
    fragmentInfo.pixelCoords = float2(pixel);
    fragmentInfo.fragPosWorldSpace = sample.positionWS;
    fragmentInfo.fragPosViewSpace = sample.positionVS;

    const float3 viewWS = normalize(mainCamera.positionWorldSpace.xyz - sample.positionWS);
    FillFragmentInfoDirect(
        fragmentInfo,
        sample.materialInputs,
        viewWS,
        float2(pixel),
        true,
        isFrontFace,
        sample.materialFlags);

    const float alpha = saturate(fragmentInfo.alpha);
    if (alpha <= 0.0f)
    {
        return;
    }

    const uint2 lowPixel = pixel / CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR;
    if (lowPixel.x >= config.lowResolutionWidth || lowPixel.y >= config.lowResolutionHeight)
    {
        return;
    }

    const uint sliceIndex = ComputeDepthSlice(config, sample.linearDepth);
    const float opticalDepth = -log(max(1.0f - alpha, 1.0e-4f));
    const uint quantizedExtinction = max(1u, (uint)round(opticalDepth * CLOD_FIXED_SLICE_SCALAR_VBOIT_EXTINCTION_QUANTIZATION_SCALE));

    RWTexture2DArray<uint> extinctionTexture = ResourceDescriptorHeap[config.extinctionUAVDescriptorIndex];
    uint ignored;
    InterlockedAdd(extinctionTexture[uint3(lowPixel, sliceIndex)], quantizedExtinction, ignored);
}