#ifndef MESHLET_COMMON_HLSLI
#define MESHLET_COMMON_HLSLI

#include "Common/defines.h"
#include "include/structs.hlsli"
#include "include/cbuffers.hlsli"
#include "include/loadingUtils.hlsli"
#include "include/vertex.hlsli"
#include "Include/clodStructs.hlsli"
#include "Include/clodPageAccess.hlsli"
#include "Include/visibleClusterPacking.hlsli"
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
    Meshlet meshlet; // Used by non-CLod path
    PerMeshBuffer meshBuffer;
    PerMeshInstanceBuffer meshInstanceBuffer;
    PerObjectBuffer objectBuffer;
    uint vertCount;
    uint triCount;
    uint vertOffset; // Non-CLod: meshlet.VertOffset; CLod: unused (0)

    // Non-CLod vertex/triangle addressing
    uint postSkinningBufferOffset;
    uint prevPostSkinningBufferOffset;
    uint groupMeshletTrianglesByteOffset; // Non-CLod triangle buffer offset

    // Per-meshlet compression from CLodMeshletDescriptor
    uint bitsX;
    uint bitsY;
    uint bitsZ;
    int3 minQ;
    uint positionBitOffset;     // bit offset within page position bitstream
    uint normalWordOffset;      // word offset within page normal array
    uint triangleByteOffset;    // byte offset within page triangle stream

    // Page-level stream base byte offsets (absolute in slab)
    uint positionBitstreamBase;
    uint normalArrayBase;
    uint triangleStreamBase;

    // Mesh-wide quantization
    uint compressedPositionQuantExp;

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
    setup.groupMeshletTrianglesByteOffset = setup.meshBuffer.clodMeshletTrianglesBufferOffset;

    // CLod per-meshlet fields unused in non-CLod path
    setup.bitsX = 0;
    setup.bitsY = 0;
    setup.bitsZ = 0;
    setup.minQ = int3(0, 0, 0);
    setup.positionBitOffset = 0;
    setup.normalWordOffset = 0;
    setup.triangleByteOffset = 0;
    setup.positionBitstreamBase = 0;
    setup.normalArrayBase = 0;
    setup.triangleStreamBase = 0;
    setup.compressedPositionQuantExp = 0;

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

    ByteAddressBuffer visibleClusters =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisibleClusterBuffer)];
    const uint3 packedCluster = CLodLoadVisibleClusterPacked(visibleClusters, visibleMeshletIndex);
    StructuredBuffer<PerMeshInstanceBuffer> meshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];

    setup.meshletIndex = CLodVisibleClusterLocalMeshletIndex(packedCluster);
    setup.meshInstanceBuffer =  meshInstanceBuffer[CLodVisibleClusterInstanceID(packedCluster)];
    setup.viewID = CLodVisibleClusterViewID(packedCluster);

    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];

    setup.meshBuffer = perMeshBuffer[setup.meshInstanceBuffer.perMeshBufferIndex];
    setup.objectBuffer = perObjectBuffer[setup.meshInstanceBuffer.perObjectBufferIndex];

    // Use pre-resolved page address from VisibleCluster
    const uint pageSlabDesc = CLodVisibleClusterPageSlabDescriptorIndex(packedCluster);
    const uint pageSlabOff  = CLodVisibleClusterPageSlabByteOffset(packedCluster);
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

    // Load per-meshlet descriptor
    CLodMeshletDescriptor desc = LoadMeshletDescriptor(
        pageSlabDesc, pageSlabOff, hdr.descriptorOffset, setup.meshletIndex);

    setup.meshlet = (Meshlet)0; // Not used in CLod path
    setup.vertCount = CLodDescVertexCount(desc);
    setup.triCount = CLodDescTriangleCount(desc);
    setup.vertOffset = 0;

    // Per-meshlet compression from descriptor
    setup.bitsX = CLodDescBitsX(desc);
    setup.bitsY = CLodDescBitsY(desc);
    setup.bitsZ = CLodDescBitsZ(desc);
    setup.minQ = int3(desc.minQx, desc.minQy, desc.minQz);
    setup.positionBitOffset = desc.positionBitOffset;
    setup.normalWordOffset = desc.normalWordOffset;
    setup.triangleByteOffset = desc.triangleByteOffset;

    // Page-level stream base offsets (absolute in slab)
    setup.positionBitstreamBase = pageSlabOff + hdr.positionBitstreamOffset;
    setup.normalArrayBase = pageSlabOff + hdr.normalArrayOffset;
    setup.triangleStreamBase = pageSlabOff + hdr.triangleStreamOffset;

    setup.compressedPositionQuantExp = hdr.compressedPositionQuantExp;
    setup.pagePoolSlabDescriptorIndex = pageSlabDesc;

    // Non-CLod fields unused
    setup.groupMeshletTrianglesByteOffset = 0;
    setup.postSkinningBufferOffset = 0;
    setup.prevPostSkinningBufferOffset = 0;

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
    ByteAddressBuffer visibleClusters =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisibleClusterBuffer)];
    (void)visibleClusters;
    return InitializeMeshletInternalCLod(visibleClusterIndex, setup);
}

uint3 DecodeTriangle(uint triLocalIndex, MeshletSetup setup)
{
    ByteAddressBuffer triangleBuffer;
    uint triOffset;

    if (setup.pagePoolSlabDescriptorIndex != 0u)
    {
        // CLod path: triangle stream in slab, addressed by per-meshlet descriptor
        triangleBuffer = ResourceDescriptorHeap[setup.pagePoolSlabDescriptorIndex];
        triOffset = setup.triangleStreamBase + setup.triangleByteOffset + triLocalIndex * 3;
    }
    else
    {
        // Non-CLod path: original triangle buffer
        triangleBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MeshResources::MeshletTriangles)];
        triOffset = setup.groupMeshletTrianglesByteOffset + setup.meshlet.TriOffset + triLocalIndex * 3;
    }

    uint alignedOffset = (triOffset / 4) * 4;
    uint firstWord = triangleBuffer.Load(alignedOffset);
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
        uint secondWord = triangleBuffer.Load(alignedOffset + 4);
        b2 = secondWord & 0xFF;
    }
    else
    { // byteOffset == 3
        // The first byte is at the last position in firstWord,
        // The next two bytes must come from the next word.
        uint secondWord = triangleBuffer.Load(alignedOffset + 4);
        b1 = secondWord & 0xFF;
        b2 = (secondWord >> 8) & 0xFF;
    }

    return uint3(b0, b1, b2);
}

#endif // MESHLET_COMMON_HLSLI