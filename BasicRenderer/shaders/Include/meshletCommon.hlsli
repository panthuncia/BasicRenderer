#ifndef MESHLET_COMMON_HLSLI
#define MESHLET_COMMON_HLSLI

#include "Common/defines.h"
#include "include/structs.hlsli"
#include "include/cbuffers.hlsli"
#include "include/loadingUtils.hlsli"
#include "include/vertex.hlsli"
#include "Include/clodStructs.hlsli"
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
    uint groupVertexBase;
    uint groupVertexCount;
    uint groupVertexChunkByteOffset;
    uint groupMeshletVerticesBase;
    uint groupMeshletVertexCount;
    uint groupMeshletTrianglesByteOffset;
    uint compressedPositionWordsBase;
    uint compressedPositionWordCount;
    uint compressedPositionBitsX;
    uint compressedPositionBitsY;
    uint compressedPositionBitsZ;
    uint compressedPositionQuantExp;
    int3 compressedPositionMinQ;
    uint compressedNormalWordsBase;
    uint compressedNormalWordCount;
    uint compressedMeshletVertexWordsBase;
    uint compressedMeshletVertexWordCount;
    uint compressedMeshletVertexBits;
    uint compressedFlags;
    uint postSkinningBufferOffset;
    uint prevPostSkinningBufferOffset;
    // Page-pool addressing (0 = non-CLod / not loaded)
    uint pagePoolSlabDescriptorIndex;  // Descriptor-heap index of the slab ByteAddressBuffer
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

    uint meshletOffset = setup.meshBuffer.clodMeshletBufferOffset;
    setup.meshlet = meshletBuffer[meshletOffset + setup.meshletIndex];

    setup.vertCount = setup.meshlet.VertCount;
    setup.triCount = setup.meshlet.TriCount;
    setup.vertOffset = setup.meshlet.VertOffset;
    setup.groupVertexBase = 0;
    setup.groupVertexCount = 0;
    setup.groupVertexChunkByteOffset = 0;
    setup.groupMeshletVerticesBase = 0;
    setup.groupMeshletVertexCount = 0;
    setup.groupMeshletTrianglesByteOffset = setup.meshBuffer.clodMeshletTrianglesBufferOffset;
    setup.compressedPositionWordsBase = 0;
    setup.compressedPositionWordCount = 0;
    setup.compressedPositionBitsX = 0;
    setup.compressedPositionBitsY = 0;
    setup.compressedPositionBitsZ = 0;
    setup.compressedPositionQuantExp = 0;
    setup.compressedPositionMinQ = int3(0, 0, 0);
    setup.compressedNormalWordsBase = 0;
    setup.compressedNormalWordCount = 0;
    setup.compressedMeshletVertexWordsBase = 0;
    setup.compressedMeshletVertexWordCount = 0;
    setup.compressedMeshletVertexBits = 0;
    setup.compressedFlags = 0;

    // setup.vertexBuffer = vertexBuffer;
    // setup.meshletTrianglesBuffer = meshletTrianglesBuffer;
    // setup.meshletVerticesBuffer = meshletVerticesBuffer;

    setup.pagePoolSlabDescriptorIndex = 0;

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

    setup.meshletIndex = cluster.globalMeshletIndex;
    setup.meshInstanceBuffer =  meshInstanceBuffer[cluster.instanceID];
    setup.viewID = cluster.viewID;

    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    StructuredBuffer<Meshlet> meshletBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletOffsets)];
    StructuredBuffer<MeshInstanceClodOffsets> clodOffsets = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
    StructuredBuffer<CLodMeshMetadata> clodMeshMetadataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::MeshMetadata)];
    StructuredBuffer<ClusterLODGroupChunk> groupChunks = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::GroupChunks)];
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    setup.meshBuffer = perMeshBuffer[setup.meshInstanceBuffer.perMeshBufferIndex];
    setup.objectBuffer = perObjectBuffer[setup.meshInstanceBuffer.perObjectBufferIndex];

    MeshInstanceClodOffsets offsets = clodOffsets[cluster.instanceID];
    CLodMeshMetadata clodMeshMetadata = clodMeshMetadataBuffer[offsets.clodMeshMetadataIndex];
    if (cluster.groupID >= clodMeshMetadata.groupChunkTableCount)
    {
        return false;
    }

    ClusterLODGroupChunk groupChunk = groupChunks[clodMeshMetadata.groupChunkTableBase + cluster.groupID];

    uint meshletStart = groupChunk.meshletBase;
    uint meshletEnd = groupChunk.meshletBase + groupChunk.meshletCount;
    if (setup.meshletIndex < meshletStart || setup.meshletIndex >= meshletEnd)
    {
        return false;
    }

    setup.meshlet = meshletBuffer[setup.meshletIndex];

    setup.vertCount = setup.meshlet.VertCount;
    setup.triCount = setup.meshlet.TriCount;
    setup.vertOffset = setup.meshlet.VertOffset;
    setup.groupVertexBase = 0;
    setup.groupVertexCount = groupChunk.groupVertexCount;
    setup.groupMeshletVertexCount = groupChunk.meshletVertexCount;
    setup.compressedPositionWordCount = groupChunk.compressedPositionWordCount;
    setup.compressedPositionBitsX = groupChunk.compressedPositionBitsX;
    setup.compressedPositionBitsY = groupChunk.compressedPositionBitsY;
    setup.compressedPositionBitsZ = groupChunk.compressedPositionBitsZ;
    setup.compressedPositionQuantExp = groupChunk.compressedPositionQuantExp;
    setup.compressedPositionMinQ = int3(
        groupChunk.compressedPositionMinQx,
        groupChunk.compressedPositionMinQy,
        groupChunk.compressedPositionMinQz);
    setup.compressedNormalWordCount = groupChunk.compressedNormalWordCount;
    setup.compressedMeshletVertexWordCount = groupChunk.compressedMeshletVertexWordCount;
    setup.compressedMeshletVertexBits = groupChunk.compressedMeshletVertexBits;
    setup.compressedFlags = groupChunk.compressedFlags;

    if (setup.vertOffset + setup.vertCount > setup.groupMeshletVertexCount)
    {
        return false;
    }

    if (setup.meshlet.TriOffset + setup.meshlet.TriCount * 3u > groupChunk.meshletTrianglesByteCount)
    {
        return false;
    }

    // setup.vertexBuffer = vertexBuffer;
    // setup.meshletTrianglesBuffer = meshletTrianglesBuffer;
    // setup.meshletVerticesBuffer = meshletVerticesBuffer;

    // ── Page-pool addressing ─────────────────────────────────────────────
    setup.pagePoolSlabDescriptorIndex = groupChunk.pagePoolSlabDescriptorIndex;
    {
        uint slabBase = groupChunk.pagePoolSlabByteOffset;
        setup.groupVertexChunkByteOffset     = slabBase + groupChunk.vertexIntraPageByteOffset;
        setup.groupMeshletVerticesBase       = (slabBase + groupChunk.meshletVertexIntraPageByteOffset) / 4u;
        setup.groupMeshletTrianglesByteOffset = slabBase + groupChunk.triangleIntraPageByteOffset;
        setup.compressedPositionWordsBase    = (slabBase + groupChunk.compPosIntraPageByteOffset) / 4u;
        setup.compressedNormalWordsBase      = (slabBase + groupChunk.compNormIntraPageByteOffset) / 4u;
        setup.compressedMeshletVertexWordsBase = (slabBase + groupChunk.compMeshletVertIntraPageByteOffset) / 4u;
    }

    // Vertex data lives in the slab.
    {
        uint slabBase = groupChunk.pagePoolSlabByteOffset;
        setup.postSkinningBufferOffset     = slabBase + groupChunk.vertexIntraPageByteOffset;
        setup.prevPostSkinningBufferOffset = setup.postSkinningBufferOffset;
    }

    if (setup.meshletIndex >= setup.meshBuffer.clodMeshletBufferOffset + setup.meshBuffer.clodNumMeshlets)
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
    StructuredBuffer<VisibleCluster> visibleClusters =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisibleClusterBuffer)];
    (void)visibleClusters;
    return InitializeMeshletInternalCLod(visibleClusterIndex, setup);
}

uint3 DecodeTriangle(uint triLocalIndex, MeshletSetup setup)
{
    ByteAddressBuffer meshletTrianglesBuffer;
    if (setup.pagePoolSlabDescriptorIndex != 0u)
        meshletTrianglesBuffer = ResourceDescriptorHeap[setup.pagePoolSlabDescriptorIndex];
    else
        meshletTrianglesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletTriangles)];

    uint triOffset = setup.groupMeshletTrianglesByteOffset + setup.meshlet.TriOffset + triLocalIndex * 3;
    uint alignedOffset = (triOffset / 4) * 4;
    uint firstWord = meshletTrianglesBuffer.Load(alignedOffset);
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
        uint secondWord = meshletTrianglesBuffer.Load(alignedOffset + 4);
        b2 = secondWord & 0xFF;
    }
    else
    { // byteOffset == 3
        // The first byte is at the last position in firstWord,
        // The next two bytes must come from the next word.
        uint secondWord = meshletTrianglesBuffer.Load(alignedOffset + 4);
        b1 = secondWord & 0xFF;
        b2 = (secondWord >> 8) & 0xFF;
    }

    return uint3(b0, b1, b2);
}

#endif // MESHLET_COMMON_HLSLI