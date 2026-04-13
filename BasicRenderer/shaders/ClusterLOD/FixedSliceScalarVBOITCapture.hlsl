#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"
#include "include/visibilityPacking.hlsli"
#include "include/clodResolveCommon.hlsli"
#include "include/FixedSliceScalarVBOITCommon.hlsli"
#include "PerPassRootConstants/clodRasterizationRootConstants.h"

[shader("pixel")]
void FixedSliceScalarVBOITCapturePSMain(VisBufferPSInput input, bool isFrontFace : SV_IsFrontFace, uint primID : SV_PrimitiveID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)];
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer = ResourceDescriptorHeap[CLOD_RASTER_FIXED_SLICE_SCALAR_VBOIT_CONFIG_DESCRIPTOR_INDEX];

    const ClodViewRasterInfo viewRasterInfo = viewRasterInfoBuffer[input.viewID];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];
#if defined(CLOD_FIXED_SLICE_SCALAR_VBOIT_OCCUPANCY_ONLY)
    if (viewRasterInfo.opaqueVisibilitySRVDescriptorIndex == 0xFFFFFFFFu ||
        config.virtualSliceCount == 0u ||
        config.occupancyUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.occupancySliceMaskUAVDescriptorIndex == 0xFFFFFFFFu)
#else
    if (viewRasterInfo.opaqueVisibilitySRVDescriptorIndex == 0xFFFFFFFFu ||
        config.sliceCount == 0u ||
        config.occupancyUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.extinctionUAVDescriptorIndex == 0xFFFFFFFFu)
#endif
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

#if defined(CLOD_FIXED_SLICE_SCALAR_VBOIT_OCCUPANCY_ONLY)
    const float virtualSliceCoordinate = ComputeBaseDepthSliceCoordinate(config, sample.linearDepth);
    const uint virtualSliceIndex0 = min((uint)floor(virtualSliceCoordinate), config.virtualSliceCount - 1u);
    const uint virtualSliceIndex1 = min(virtualSliceIndex0 + 1u, config.virtualSliceCount - 1u);
    RWTexture2D<float> occupancyTexture = ResourceDescriptorHeap[config.occupancyUAVDescriptorIndex];
    RWTexture2D<uint> occupancySliceMaskTexture = ResourceDescriptorHeap[config.occupancySliceMaskUAVDescriptorIndex];
    occupancyTexture[lowPixel] = 1.0f;

    const uint sliceBit0 = 1u << virtualSliceIndex0;
    const uint sliceBit1 = 1u << virtualSliceIndex1;
    const uint occupancySliceMask = virtualSliceIndex0 == virtualSliceIndex1 ? sliceBit0 : (sliceBit0 | sliceBit1);
    uint ignoredOccupancyMask;
    InterlockedOr(occupancySliceMaskTexture[lowPixel], occupancySliceMask, ignoredOccupancyMask);
    return;
#endif

    const float sliceCoordinate = ComputeDepthSliceCoordinate(config, sample.linearDepth);
    const uint sliceIndex0 = min((uint)floor(sliceCoordinate), config.sliceCount - 1u);
    const uint sliceIndex1 = min(sliceIndex0 + 1u, config.sliceCount - 1u);

    const float lowResolutionPixelCoverage = rcp((float)(
        CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR *
        CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR));
    const float opticalDepth = -log(max(1.0f - alpha, 1.0e-4f)) * lowResolutionPixelCoverage;
    const float sliceWeight1 = saturate(sliceCoordinate - (float)sliceIndex0);
    const float sliceWeight0 = 1.0f - sliceWeight1;

    uint quantizedExtinction0 = (uint)round(opticalDepth * sliceWeight0 * CLOD_FIXED_SLICE_SCALAR_VBOIT_EXTINCTION_QUANTIZATION_SCALE);
    uint quantizedExtinction1 = (uint)round(opticalDepth * sliceWeight1 * CLOD_FIXED_SLICE_SCALAR_VBOIT_EXTINCTION_QUANTIZATION_SCALE);
    if (quantizedExtinction0 == 0u && quantizedExtinction1 == 0u)
    {
        if (sliceWeight0 >= sliceWeight1)
        {
            quantizedExtinction0 = 1u;
        }
        else
        {
            quantizedExtinction1 = 1u;
        }
    }

    RWTexture2DArray<uint> extinctionTexture = ResourceDescriptorHeap[config.extinctionUAVDescriptorIndex];
    uint ignored;
    if (quantizedExtinction0 != 0u)
    {
        InterlockedAdd(extinctionTexture[uint3(lowPixel, sliceIndex0)], quantizedExtinction0, ignored);
    }
    if (quantizedExtinction1 != 0u)
    {
        InterlockedAdd(extinctionTexture[uint3(lowPixel, sliceIndex1)], quantizedExtinction1, ignored);
    }
}