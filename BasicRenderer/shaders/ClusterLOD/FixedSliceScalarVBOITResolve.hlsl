#include "include/cbuffers.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITResolveRootConstants.h"

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodFixedSliceScalarVBOITResolveCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
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
    Texture2D<float> shadingExtinctionTexture = ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_RESOLVE_SHADING_EXTINCTION_DESCRIPTOR_INDEX];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];

    const uint2 pixel = dispatchThreadId.xy;
    const float4 accumulated = accumulationTexture[pixel];
    const float accumulatedExtinction = max(shadingExtinctionTexture[pixel], 0.0f);
    if (accumulated.a <= 1.0e-5f && accumulatedExtinction <= 1.0e-5f)
    {
        return;
    }

    const float4 existingHdr = hdrTarget[pixel];
    const float residualTransmittance = exp(-accumulatedExtinction);
    const float transparentCoverage = saturate(1.0f - residualTransmittance);
    const float normalizationWeight = max(accumulated.a, 1.0e-5f);
    const float3 transparentColor = accumulated.a > 1.0e-5f
        ? accumulated.rgb / normalizationWeight
        : 0.0f.xxx;
    hdrTarget[pixel] = float4(
        transparentColor * transparentCoverage + existingHdr.rgb * residualTransmittance,
        saturate(transparentCoverage + existingHdr.a * residualTransmittance));
}
