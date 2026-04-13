#include "include/cbuffers.hlsli"
#include "include/AVBOITCommon.hlsli"
#include "PerPassRootConstants/clodAVBOITResolveRootConstants.h"

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodAVBOITResolveCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    RWTexture2D<float4> hdrTarget = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Color::HDRColorTarget)];
    uint width, height;
    hdrTarget.GetDimensions(width, height);
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
    {
        return;
    }

    StructuredBuffer<CLodAVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_RESOLVE_CONFIG_DESCRIPTOR_INDEX];
    Texture2D<float4> accumulationTexture = ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_RESOLVE_ACCUMULATION_DESCRIPTOR_INDEX];
    Texture2D<float4> normalizationTexture = ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_RESOLVE_NORMALIZATION_DESCRIPTOR_INDEX];
    Texture2D<float4> shadingExtinctionTexture = ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_RESOLVE_SHADING_EXTINCTION_DESCRIPTOR_INDEX];
    const CLodAVBOITConfig config = configBuffer[0];

    const uint2 pixel = dispatchThreadId.xy;
    const float4 accumulated = accumulationTexture[pixel];
    const float3 accumulatedWeight = max(normalizationTexture[pixel].rgb, 0.0f.xxx);
    const float3 accumulatedExtinction = max(shadingExtinctionTexture[pixel].rgb, 0.0f.xxx);
    if (max(accumulatedWeight.r, max(accumulatedWeight.g, accumulatedWeight.b)) <= 1.0e-5f &&
        max(accumulatedExtinction.r, max(accumulatedExtinction.g, accumulatedExtinction.b)) <= 1.0e-5f)
    {
        return;
    }

    const float4 existingHdr = hdrTarget[pixel];
    const float3 residualTransmittance = exp(-accumulatedExtinction);
    const float3 transparentCoverage = saturate(1.0f.xxx - residualTransmittance);
    const float3 transparentColor = accumulated.rgb / max(accumulatedWeight, 1.0e-5f.xxx);
    const float residualAlpha = GetAVBOITLuma(residualTransmittance);
    const float transparentCoverageAlpha = saturate(1.0f - residualAlpha);
    hdrTarget[pixel] = float4(
        transparentColor * transparentCoverage + existingHdr.rgb * residualTransmittance,
        saturate(transparentCoverageAlpha + existingHdr.a * residualAlpha));
}
