#include "cbuffers.hlsli"

struct IndirectCommand {
    uint perObjectBufferIndex;
    uint perMeshBufferIndex;
    uint3 dispatchMeshArguments;
};

[numthreads(64, 1, 1)]
void CSMain(uint dispatchID : SV_DispatchThreadID) {
    if (dispatchID > maxDrawIndex) {
        return;
    }
    StructuredBuffer<unsigned int> activeDrawSetIndicesBuffer = ResourceDescriptorHeap[activeDrawSetIndicesBufferDescriptorIndex];
    StructuredBuffer<IndirectCommand> indirectCommandBuffer = ResourceDescriptorHeap[drawSetCommandBufferDescriptorIndex];
    AppendStructuredBuffer<IndirectCommand> indirectCommandOutputBuffer = ResourceDescriptorHeap[indirectCommandBufferDescriptorIndex];
    
    uint index = activeDrawSetIndicesBuffer[dispatchID];
    IndirectCommand command = indirectCommandBuffer[index];
    indirectCommandOutputBuffer.Append(command);
}