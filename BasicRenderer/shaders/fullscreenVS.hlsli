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

/*static const float2 QuadCorners[4] =
{
    float2(-1, -1), float2(1, -1),
    float2(-1, 1), float2(1, 1),
};*/

FULLSCREEN_VS_OUTPUT FullscreenVSMain(uint vid : SV_VertexID)
{
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<Camera> cameraBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    FULLSCREEN_VS_OUTPUT o;
    float2 pos = 0;
    pos.x = (vid == 1) ? +3.0 : -1.0;
    pos.y = (vid == 2) ? +3.0 : -1.0;
    
    o.position = float4(pos, 1.0f, 1);
    o.uv = pos * 0.5 + 0.5;
    // build a ray in clip space:
    float4 clipRay = float4(pos, -1, 1);

    float4 viewH = mul(clipRay, cameraBuffer[perFrameBuffer.mainCameraIndex].projectionInverse);

    o.viewRayVS = viewH.xyz;
    return o;
}

#endif // __FULLSCREEN_VS_HLSLI__