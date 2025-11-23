#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"

[numthreads(8, 8, 1)]
void GBufferConstructionCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    uint screenW = perFrameBuffer.screenResX;
    uint screenH = perFrameBuffer.screenResY;

    if (dispatchThreadId.x >= screenW || dispatchThreadId.y >= screenH)
    {
        return;
    }
    
    uint2 pixel = dispatchThreadId.xy;
    // [0] = 8 bits for meshlet-local vertex index and 24 bits for draw call ID
    // [1] = drawcall-local meshlet index
    Texture2D<uint> visibilityTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibilityTexture)];
    uint visibilityData = visibilityTexture[pixel];
    uint drawCallMeshletIndex = visibilityData.y;
    uint drawCallVertexIndex = visibilityData.x & 0xFF;
    uint drawCallID = visibilityData.x >> 8;
}