#ifndef MESHLET_COMMON_HLSLI
#define MESHLET_COMMON_HLSLI

#include "Common/defines.h"
#include "include/structs.hlsli"
#include "include/cbuffers.hlsli"
#include "include/loadingUtils.hlsli"
#include "include/vertex.hlsli"
#include "Include/clodStructs.hlsli"
#include "Include/clodPageAccess.hlsli"
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

// Load a Meshlet from a page-pool slab ByteAddressBuffer.
// meshletByteAddr = slabByteOffset + meshletIntraPageByteOffset + localMeshletIndex * 16
Meshlet LoadMeshletFromSlab(uint slabDescriptorIndex, uint meshletByteAddr)
{
    ByteAddressBuffer slabBuffer = ResourceDescriptorHeap[slabDescriptorIndex];
    return LoadMeshlet(slabBuffer.Load4(meshletByteAddr));
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

    setup.meshletIndex = cluster.localMeshletIndex;
    setup.meshInstanceBuffer =  meshInstanceBuffer[cluster.instanceID];
    setup.viewID = cluster.viewID;

    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    setup.meshBuffer = perMeshBuffer[setup.meshInstanceBuffer.perMeshBufferIndex];
    setup.objectBuffer = perObjectBuffer[setup.meshInstanceBuffer.perObjectBufferIndex];

    // Use pre-resolved page address from VisibleCluster
    const uint pageSlabDesc = cluster.pageSlabDescriptorIndex;
    const uint pageSlabOff  = cluster.pageSlabByteOffset;
    if (pageSlabDesc == 0)
    {
        return false;
    }

    CLodPageHeader hdr = LoadPageHeader(pageSlabDesc, pageSlabOff);

    // meshletIndex is page-local
    if (setup.meshletIndex >= hdr.meshletCount)
    {
        return false;
    }

    // Load meshlet from the page-pool slab
    {
        uint meshletAddr = pageSlabOff + hdr.meshletStructOffset + setup.meshletIndex * 16u;
        setup.meshlet = LoadMeshletFromSlab(pageSlabDesc, meshletAddr);
    }

    setup.vertCount = setup.meshlet.VertCount;
    setup.triCount = setup.meshlet.TriCount;
    setup.vertOffset = setup.meshlet.VertOffset;
    setup.groupVertexBase = 0;
    setup.groupVertexCount = hdr.vertexCount;
    setup.groupMeshletVertexCount = hdr.meshletVertexCount;
    setup.compressedPositionWordCount = hdr.compressedPositionWordCount;
    setup.compressedPositionBitsX = hdr.compressedPositionBitsX;
    setup.compressedPositionBitsY = hdr.compressedPositionBitsY;
    setup.compressedPositionBitsZ = hdr.compressedPositionBitsZ;
    setup.compressedPositionQuantExp = hdr.compressedPositionQuantExp;
    setup.compressedPositionMinQ = int3(
        hdr.compressedPositionMinQx,
        hdr.compressedPositionMinQy,
        hdr.compressedPositionMinQz);
    setup.compressedNormalWordCount = hdr.compressedNormalWordCount;
    setup.compressedMeshletVertexWordCount = hdr.compressedMeshletVertexWordCount;
    setup.compressedMeshletVertexBits = hdr.compressedMeshletVertexBits;
    setup.compressedFlags = hdr.compressedFlags;

    if (setup.vertOffset + setup.vertCount > setup.groupMeshletVertexCount)
    {
        return false;
    }

    if (setup.meshlet.TriOffset + setup.meshlet.TriCount * 3u > hdr.meshletTrianglesByteCount)
    {
        return false;
    }

    // ── Page-pool addressing from pre-resolved page location ─────────────
    setup.pagePoolSlabDescriptorIndex = pageSlabDesc;
    {
        uint base = pageSlabOff;
        setup.groupVertexChunkByteOffset     = base + hdr.vertexOffset;
        setup.groupMeshletVerticesBase       = (base + hdr.meshletVertexOffset) / 4u;
        setup.groupMeshletTrianglesByteOffset = base + hdr.triangleOffset;
        setup.compressedPositionWordsBase    = (base + hdr.compPosOffset) / 4u;
        setup.compressedNormalWordsBase      = (base + hdr.compNormOffset) / 4u;
        setup.compressedMeshletVertexWordsBase = (base + hdr.compMeshletVertOffset) / 4u;
    }

    // Vertex data lives in the slab page.
    {
        setup.postSkinningBufferOffset     = pageSlabOff + hdr.vertexOffset;
        setup.prevPostSkinningBufferOffset = setup.postSkinningBufferOffset;
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