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

    Texture2D<float4> accumulationTexture = ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_RESOLVE_ACCUMULATION_DESCRIPTOR_INDEX];

    const uint2 pixel = dispatchThreadId.xy;
    const float4 accumulated = accumulationTexture[pixel];
    if (accumulated.a <= 1.0e-5f)
    {
        return;
    }

    const float4 existingHdr = hdrTarget[pixel];
    const float residualTransmittance = saturate(1.0f - accumulated.a);
    hdrTarget[pixel] = float4(
        accumulated.rgb + existingHdr.rgb * residualTransmittance,
        accumulated.a + existingHdr.a * residualTransmittance);
}
