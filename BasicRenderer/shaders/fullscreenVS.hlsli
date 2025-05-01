#ifndef __FULLSCREEN_VS_HLSLI__
#define __FULLSCREEN_VS_HLSLI__

#include "structs.hlsli"
#include "cbuffers.hlsli"

struct FULLSCREEN_VS_OUTPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD1;
    float3 viewRayVS : TEXCOORD2;
};

static const float2 QuadCorners[4] =
{
    float2(-1, -1), float2(1, -1),
    float2(-1, 1), float2(1, 1),
};

FULLSCREEN_VS_OUTPUT VSMain(float3 pos : POSITION, float2 uv : TEXCOORD0, uint vid : SV_VertexID)
{
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<Camera> cameraBuffer = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    
    FULLSCREEN_VS_OUTPUT o;
    float2 ndc = QuadCorners[vid];
    o.position = float4(ndc, 1.0f, 1);
    o.uv = ndc * 0.5 + 0.5;

    // build a ray in clip space:
    float4 clipRay = float4(ndc, -1, 1);

    float4 viewH = mul(clipRay, cameraBuffer[perFrameBuffer.mainCameraIndex].projectionInverse);

    o.viewRayVS = viewH.xyz;
    return o;
}

#endif // __FULLSCREEN_VS_HLSLI__