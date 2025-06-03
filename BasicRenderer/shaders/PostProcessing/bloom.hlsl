#include "cbuffers.hlsli"
#include "structs.hlsli"
#include "fullscreenVS.hlsli"
#include "gammaCorrection.hlsli"

// UintRootConstant0 is HDR source SRV
float4 PSMain(FULLSCREEN_VS_OUTPUT input) : SV_Target
{
    Texture2D<float4> hdrSource = ResourceDescriptorHeap[UintRootConstant0];
    float2 uv = input.uv;
    uv.y = 1.0f - uv.y; // Flip the Y coordinate for correct sampling in some cases
    float4 color = hdrSource.SampleLevel(g_pointClamp, uv, 0);
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

	
    return color;
}