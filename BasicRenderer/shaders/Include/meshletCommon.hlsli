#ifndef MESHLET_COMMON_HLSLI
#define MESHLET_COMMON_HLSLI

#include "Common/defines.h"
#include "include/structs.hlsli"
#include "include/cbuffers.hlsli"
#include "include/loadingUtils.hlsli"
#include "include/vertex.hlsli"
// Meshlet description
struct Meshlet
{
    uint VertOffset;
    uint TriOffset;
    uint VertCount;
    uint TriCount;
};

Meshlet LoadMeshlet(uint4 raw)
{
    Meshlet m;
    m.VertOffset = raw.x;
    m.TriOffset = raw.y;
    m.VertCount = raw.z;
    m.TriCount = raw.w;
    return m;
}

struct MeshletSetup
{
    uint viewID; // Which view this meshlet is being rendered for (for CLod path)
    uint meshletIndex;
    Meshlet meshlet;
    PerMeshBuffer meshBuffer;
    PerMeshInstanceBuffer meshInstanceBuffer;
    PerObjectBuffer objectBuffer;
    uint vertCount;
    uint triCount;
    uint vertOffset;
    uint postSkinningBufferOffset;
    uint prevPostSkinningBufferOffset;
    ByteAddressBuffer vertexBuffer;
    ByteAddressBuffer meshletTrianglesBuffer;
    StructuredBuffer<uint> meshletVerticesBuffer;
};

// Internal common initialization (indices already chosen)
bool InitializeMeshletInternal(
    uint meshletLocalIndex,
    PerMeshInstanceBuffer meshInstance,
    out MeshletSetup setup)
{
    setup.meshletIndex = meshletLocalIndex;
    setup.meshInstanceBuffer = meshInstance;
    setup.viewID = 0; // Unused

    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    StructuredBuffer<Meshlet> meshletBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletOffsets)];
    StructuredBuffer<uint> meshletVerticesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletVertexIndices)];
    ByteAddressBuffer meshletTrianglesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletTriangles)];
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostSkinningVertices)];
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    setup.meshBuffer = perMeshBuffer[meshInstance.perMeshBufferIndex];
    setup.objectBuffer = perObjectBuffer[meshInstance.perObjectBufferIndex];

    uint meshletOffset = setup.meshBuffer.meshletBufferOffset;
    setup.meshlet = meshletBuffer[meshletOffset + setup.meshletIndex];

    setup.vertCount = setup.meshlet.VertCount;
    setup.triCount = setup.meshlet.TriCount;
    setup.vertOffset = setup.meshlet.VertOffset;

    setup.vertexBuffer = vertexBuffer;
    setup.meshletTrianglesBuffer = meshletTrianglesBuffer;
    setup.meshletVerticesBuffer = meshletVerticesBuffer;

    uint postSkinningBase = meshInstance.postSkinningVertexBufferOffset;
    setup.postSkinningBufferOffset = postSkinningBase;
    setup.prevPostSkinningBufferOffset = postSkinningBase;

    if (setup.meshBuffer.vertexFlags & VERTEX_SKINNED)
    {
        uint stride = setup.meshBuffer.vertexByteSize * setup.meshBuffer.numVertices;
        setup.postSkinningBufferOffset += stride * (perFrameBuffer.frameIndex % 2);
        setup.prevPostSkinningBufferOffset += stride * ((perFrameBuffer.frameIndex + 1) % 2);
    }

    if (setup.meshletIndex >= setup.meshBuffer.numMeshlets)
    {
        return false;
    }

    return true;
}

bool InitializeMeshletInternalCLod(
    uint visibleMeshletIndex,
    out MeshletSetup setup)
{

    StructuredBuffer<VisibleCluster> visibleClusters =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisibleClusterBuffer)];
    VisibleCluster cluster = visibleClusters[visibleMeshletIndex];
    StructuredBuffer<PerMeshInstanceBuffer> meshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];

    setup.meshletIndex = cluster.meshletID;
    setup.meshInstanceBuffer =  meshInstanceBuffer[cluster.instanceID];
    setup.viewID = cluster.viewID;

    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    StructuredBuffer<Meshlet> meshletBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Meshlets)];
    StructuredBuffer<uint> meshletVerticesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletVertexIndices)];
    ByteAddressBuffer meshletTrianglesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletTriangles)];
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostSkinningVertices)];
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    setup.meshBuffer = perMeshBuffer[setup.meshInstanceBuffer.perMeshBufferIndex];
    setup.objectBuffer = perObjectBuffer[setup.meshInstanceBuffer.perObjectBufferIndex];

    uint meshletOffset = setup.meshBuffer.cLODMeshletBufferOffset;
    setup.meshlet = meshletBuffer[meshletOffset + setup.meshletIndex];

    setup.vertCount = setup.meshlet.VertCount;
    setup.triCount = setup.meshlet.TriCount;
    setup.vertOffset = setup.meshlet.VertOffset;

    setup.vertexBuffer = vertexBuffer;
    setup.meshletTrianglesBuffer = meshletTrianglesBuffer;
    setup.meshletVerticesBuffer = meshletVerticesBuffer;

    uint postSkinningBase = setup.meshInstanceBuffer.postSkinningVertexBufferOffset;
    setup.postSkinningBufferOffset = postSkinningBase;
    setup.prevPostSkinningBufferOffset = postSkinningBase;

    if (setup.meshBuffer.vertexFlags & VERTEX_SKINNED)
    {
        uint stride = setup.meshBuffer.vertexByteSize * setup.meshBuffer.numVertices;
        setup.postSkinningBufferOffset += stride * (perFrameBuffer.frameIndex % 2);
        setup.prevPostSkinningBufferOffset += stride * ((perFrameBuffer.frameIndex + 1) % 2);
    }

    if (setup.meshletIndex >= setup.meshBuffer.numMeshlets)
    {
        return false;
    }

    return true;
}

// per-draw invocation (mesh shader path uses global root constants set externally)
// Mesh shader path (root constant perMeshInstanceBufferIndex already set)
bool InitializeMeshlet(uint meshletLocalIndex, out MeshletSetup setup)
{
    StructuredBuffer<PerMeshInstanceBuffer> meshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstance = meshInstanceBuffer[perMeshInstanceBufferIndex];
    return InitializeMeshletInternal(meshletLocalIndex, meshInstance, setup);
}

// Screen-space / compute path: drawCallID directly indexes PerMeshInstanceBuffer
bool InitializeMeshletFromDrawCall(uint drawCallID, uint meshletLocalIndex, out MeshletSetup setup)
{
    StructuredBuffer<PerMeshInstanceBuffer> meshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstance = meshInstanceBuffer[drawCallID];
    return InitializeMeshletInternal(meshletLocalIndex, meshInstance, setup);
}

// Cluster LOD path:
bool InitializeMeshletFromVisibleCluster(uint visibleClusterIndex, out MeshletSetup setup)
{
    RWStructuredBuffer<VisibleCluster> visibleClusters =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisibleClusterBuffer)];
    VisibleCluster cluster = visibleClusters[visibleClusterIndex];

    StructuredBuffer<PerMeshInstanceBuffer> meshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstance = meshInstanceBuffer[cluster.instanceID];
    return InitializeMeshletInternal(cluster.meshletID, meshInstance, setup);
}

uint3 DecodeTriangle(uint triLocalIndex, MeshletSetup setup)
{
    uint triOffset = setup.meshBuffer.meshletTrianglesBufferOffset + setup.meshlet.TriOffset + triLocalIndex * 3;
    uint alignedOffset = (triOffset / 4) * 4;
    uint firstWord = setup.meshletTrianglesBuffer.Load(alignedOffset);
    uint byteOffset = triOffset % 4;

    // Load first byte
    uint b0 = (firstWord >> (byteOffset * 8)) & 0xFF;
    uint b1, b2;

    if (byteOffset <= 1)
    {
        // All three bytes are within the same word
        b1 = (firstWord >> ((byteOffset + 1) * 8)) & 0xFF;
        b2 = (firstWord >> ((byteOffset + 2) * 8)) & 0xFF;
    }
    else if (byteOffset == 2)
    {
        // The second byte is in this word, but the third byte spills into the next word
        b1 = (firstWord >> ((byteOffset + 1) * 8)) & 0xFF;
        uint secondWord = setup.meshletTrianglesBuffer.Load(alignedOffset + 4);
        b2 = secondWord & 0xFF;
    }
    else
    { // byteOffset == 3
        // The first byte is at the last position in firstWord,
        // The next two bytes must come from the next word.
        uint secondWord = setup.meshletTrianglesBuffer.Load(alignedOffset + 4);
        b1 = secondWord & 0xFF;
        b2 = (secondWord >> 8) & 0xFF;
    }

    return uint3(b0, b1, b2);
}

#endif // MESHLET_COMMON_HLSLI