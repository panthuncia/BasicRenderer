#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/loadingUtils.hlsli"
#include "include/meshletPayload.hlsli"
#include "PerPassRootConstants/amplificationShaderRootConstants.h"
#include "Common/defines.h"

groupshared Payload s_Payload;
[NumThreads(AS_GROUP_SIZE, 1, 1)]
void ASMain(uint uGroupThreadID : SV_GroupThreadID, uint uDispatchThreadID : SV_DispatchThreadID, uint uGroupID : SV_GroupID)
{
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    
    ByteAddressBuffer meshletCullingBitfieldBuffer = ResourceDescriptorHeap[MESHLET_CULLING_BITFIELD_BUFFER_SRV_DESCRIPTOR_INDEX];
    unsigned int meshletBitfieldIndex = meshInstanceBuffer.meshletBitfieldStartIndex + uDispatchThreadID;
 
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    // Culling handled in compute shader
    bool visible = uDispatchThreadID < meshBuffer.numMeshlets && !GetBit(meshletCullingBitfieldBuffer, meshletBitfieldIndex);

    if (visible)
    {
        uint index = WavePrefixCountBits(visible);
        s_Payload.MeshletIndices[index] = uDispatchThreadID;
    }
    
    uint visibleCount = WaveActiveCountBits(visible);
    DispatchMesh(visibleCount, 1, 1, s_Payload);
}