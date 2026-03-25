#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/visUtilCommon.hlsli"
#include "gbuffer.hlsl"

// Root constants (via ExecuteIndirect / command signature):
//   UintRootConstant0 = materialId
//   UintRootConstant1 = baseOffset into PixelListBuffer
//   UintRootConstant2 = count (number of pixels for this material)
[shader("compute")]
[numthreads(MATERIAL_EXECUTION_GROUP_SIZE, 1, 1)]
void EvaluateMaterialGroupCS(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint groupIndex : SV_GroupIndex
)
{
    uint baseOffset = UintRootConstant1;
    uint count = UintRootConstant2;
    uint dispatchXDimension = UintRootConstant3;

    uint idx = dispatchThreadId.y * dispatchXDimension + dispatchThreadId.x;
    if (idx >= count)
    {
        return;
    }

    StructuredBuffer<PixelRef> pixelList = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisUtil::PixelListBuffer)];

    PixelRef ref = pixelList[baseOffset + idx];

    uint2 pixel;
    pixel.x = ref.pixelXY & 0xFFFFu;
    pixel.y = ref.pixelXY >> 16;

    EvaluateGBufferOptimized(pixel);
}
