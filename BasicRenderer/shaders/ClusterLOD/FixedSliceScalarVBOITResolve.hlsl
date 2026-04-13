#include "include/cbuffers.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITResolveRootConstants.h"

float SampleBackgroundTransmittance(
    Texture2DArray<float> integratedTransmittanceTexture,
    CLodFixedSliceScalarVBOITConfig config,
    PerFrameBuffer perFrameBuffer,
    uint2 pixel)
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
    return integratedTransmittanceTexture.SampleLevel(
        g_linearClamp,
        float3(uv, (float)(config.sliceCount - 1u)),
        0.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodFixedSliceScalarVBOITResolveCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)];
    RWTexture2D<float4> hdrTarget = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Color::HDRColorTarget)];
    uint width, height;
    hdrTarget.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_RESOLVE_CONFIG_DESCRIPTOR_INDEX];
    Texture2D<float4> accumulationTexture = ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_RESOLVE_ACCUMULATION_DESCRIPTOR_INDEX];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];
    Texture2DArray<float> integratedTransmittanceTexture = ResourceDescriptorHeap[config.shadingTransmittanceSRVDescriptorIndex];

    const uint2 pixel = dispatchThreadId.xy;
    const float4 accumulated = accumulationTexture[pixel];
    if (accumulated.a <= 1.0e-5f)
    {
        return;
    }

    const float4 existingHdr = hdrTarget[pixel];
    const float residualTransmittance = saturate(SampleBackgroundTransmittance(
        integratedTransmittanceTexture,
        config,
        perFrameBuffer,
        pixel));
    const float transparentCoverage = saturate(1.0f - residualTransmittance);
    const float normalizationWeight = max(accumulated.a, 1.0e-5f);
    const float3 transparentColor = accumulated.rgb / normalizationWeight;
    hdrTarget[pixel] = float4(
        transparentColor * transparentCoverage + existingHdr.rgb * residualTransmittance,
        saturate(transparentCoverage + existingHdr.a * residualTransmittance));
}
