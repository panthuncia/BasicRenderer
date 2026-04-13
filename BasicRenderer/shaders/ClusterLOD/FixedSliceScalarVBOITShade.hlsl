#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/lighting.hlsli"
#include "include/clodResolveCommon.hlsli"
#include "include/utilities.hlsli"
#include "include/visibilityPacking.hlsli"
#include "include/FixedSliceScalarVBOITCommon.hlsli"
#include "PerPassRootConstants/clodRasterizationRootConstants.h"

struct FixedSliceScalarVBOITShadeOutput
{
    float4 accumulation : SV_Target0;
    float shadingExtinction : SV_Target1;
};

FixedSliceScalarVBOITShadeOutput MakeFixedSliceScalarVBOITShadeOutput(float4 accumulation, float shadingExtinction)
{
    FixedSliceScalarVBOITShadeOutput output;
    output.accumulation = accumulation;
    output.shadingExtinction = shadingExtinction;
    return output;
}

float SampleIntegratedTransmittance(
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
    const float biasedSliceCoordinate = ComputeLookupSliceCoordinate(config, sliceCoordinate);
    const uint sliceIndex = min((uint)floor(biasedSliceCoordinate), config.sliceCount - 1u);
    const float sliceLerpFactor = saturate(biasedSliceCoordinate - (float)sliceIndex);
    const float previousTransmittance = sliceIndex == 0u
        ? 1.0f
        : integratedTransmittanceTexture.SampleLevel(g_linearClamp, float3(uv, (float)(sliceIndex - 1u)), 0.0f);
    const float currentTransmittance = integratedTransmittanceTexture.SampleLevel(g_linearClamp, float3(uv, (float)sliceIndex), 0.0f);
    return lerp(previousTransmittance, currentTransmittance, sliceLerpFactor);
}

[shader("pixel")]
FixedSliceScalarVBOITShadeOutput FixedSliceScalarVBOITShadePSMain(VisBufferPSInput input, bool isFrontFace : SV_IsFrontFace, uint primID : SV_PrimitiveID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)];
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer = ResourceDescriptorHeap[CLOD_RASTER_FIXED_SLICE_SCALAR_VBOIT_CONFIG_DESCRIPTOR_INDEX];

    const ClodViewRasterInfo viewRasterInfo = viewRasterInfoBuffer[input.viewID];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];
    if (viewRasterInfo.opaqueVisibilitySRVDescriptorIndex == 0xFFFFFFFFu ||
        config.shadingTransmittanceSRVDescriptorIndex == 0xFFFFFFFFu ||
        config.sliceCount == 0u)
    {
        return MakeFixedSliceScalarVBOITShadeOutput(0.0f.xxxx, 0.0f);
    }

    const uint2 pixel = input.position.xy;
    if (pixel.x < viewRasterInfo.scissorMinX ||
        pixel.y < viewRasterInfo.scissorMinY ||
        pixel.x >= viewRasterInfo.scissorMaxX ||
        pixel.y >= viewRasterInfo.scissorMaxY)
    {
        return MakeFixedSliceScalarVBOITShadeOutput(0.0f.xxxx, 0.0f);
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
            return MakeFixedSliceScalarVBOITShadeOutput(0.0f.xxxx, 0.0f);
        }
    }

    ClodResolvedSample sample;
    if (!ResolveClodSampleFromVisKeyWithFace(PackVisKey(input.linearDepth, input.visibleClusterIndex, primID), pixel, !isFrontFace, sample))
    {
        return MakeFixedSliceScalarVBOITShadeOutput(0.0f.xxxx, 0.0f);
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
        return MakeFixedSliceScalarVBOITShadeOutput(0.0f.xxxx, 0.0f);
    }

    const float sliceCoordinate = ComputeDepthSliceCoordinate(config, sample.linearDepth);
    Texture2DArray<float> integratedTransmittanceTexture = ResourceDescriptorHeap[config.shadingTransmittanceSRVDescriptorIndex];
    const float transmittance = SampleIntegratedTransmittance(
        integratedTransmittanceTexture,
        config,
        perFrameBuffer,
        pixel,
        sliceCoordinate);
    if (transmittance <= max(config.zeroTransmittanceThreshold, 1.0e-4f))
    {
        return MakeFixedSliceScalarVBOITShadeOutput(0.0f.xxxx, 0.0f);
    }

    LightingOutput lightingOutput = lightFragment(
        fragmentInfo,
        mainCamera,
        perFrameBuffer.activeEnvironmentIndex,
        ResourceDescriptorIndex(Builtin::Environment::InfoBuffer),
        isFrontFace);

    const float weight = alpha * transmittance;
    const float shadingExtinction = -log(max(1.0f - alpha, 1.0e-4f)) * transmittance;
    return MakeFixedSliceScalarVBOITShadeOutput(
        float4(lightingOutput.lighting * weight, weight),
        shadingExtinction);
}
