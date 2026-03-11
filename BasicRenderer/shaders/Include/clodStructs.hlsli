#ifndef CLOD_STRUCTS_HLSLI
#define CLOD_STRUCTS_HLSLI

struct MeshInstanceClodOffsets
{
    uint clodMeshMetadataIndex;
};

struct CLodMeshMetadata
{
    uint groupsBase;
    uint segmentsBase;
    uint lodNodesBase;
    uint rootNode; // node index (relative to lodNodesBase) to start traversal from
    uint groupChunkTableBase;
    uint groupChunkTableCount;
    uint pageMapBase; // global offset into GroupPageMap buffer for this mesh
    uint pad0;
};

// GPU-visible page table entry - maps a virtual page ID to a slab + byte offset.
struct PageTableEntry
{
    uint slabIndex;      // Which slab ByteAddressBuffer this page lives in.
    uint slabByteOffset; // Byte offset of the page start within that slab.
};

// Embedded at byte 0 of each page-tile. Self-contained: all stream offsets + compression params.
// 32 x uint32 = 128 bytes. Load via 8 x Load4 from slab ByteAddressBuffer.
struct CLodPageHeader
{
    uint vertexCount;
    uint meshletCount;
    uint meshletVertexCount;
    uint meshletTrianglesByteCount;

    uint compressedPositionWordCount;
    uint compressedNormalWordCount;
    uint compressedMeshletVertexWordCount;

    // Byte offsets of each stream relative to page start
    uint vertexOffset;
    uint meshletVertexOffset;
    uint triangleOffset;
    uint compPosOffset;
    uint compNormOffset;
    uint compMeshletVertOffset;
    uint meshletStructOffset;
    uint boundsOffset;

    // Compression parameters
    uint compressedPositionBitsX;
    uint compressedPositionBitsY;
    uint compressedPositionBitsZ;
    uint compressedPositionQuantExp;
    int  compressedPositionMinQx;
    int  compressedPositionMinQy;
    int  compressedPositionMinQz;
    uint compressedMeshletVertexBits;
    uint compressedFlags;

    uint reserved0;
    uint reserved1;
    uint reserved2;
    uint reserved3;
    uint reserved4;
    uint reserved5;
    uint reserved6;
    uint reserved7;
};

// Runtime-filled entry: maps group-local page index to physical slab location.
struct GroupPageMapEntry
{
    uint slabDescriptorIndex; // Descriptor-heap index of the slab BAB
    uint slabByteOffset;      // Byte offset of page start in slab
};

struct CLodStreamingRequest
{
    uint groupGlobalIndex;
    uint meshInstanceIndex;
    uint meshBufferIndex;
    uint viewId; // low 16 bits: viewId, high 16 bits: quantized priority
};

struct CLodStreamingRuntimeState
{
    uint activeGroupScanCount;
    uint unloadAfterFrames;
    uint activeGroupsBitsetWordCount;
    uint pad2;
};
struct ClodBounds
{
    float4 centerAndRadius; // xyz = center, w = radius
    float error; // simplification error in mesh space
};

struct ClusterLODGroupSegment
{
    int refinedGroup; // -1 => terminal meshlets bucket
    uint firstMeshletInPage; // page-local start meshlet index
    uint meshletCount;
    uint pageIndex; // group-local page index (0..pageCount-1)
};

struct ClusterLODGroup
{
    ClodBounds bounds; // center/radius/error
    
    uint firstMeshlet;
    uint meshletCount;
    int depth;

    uint firstGroupVertex;
    uint groupVertexCount;
    uint firstSegment;
    uint segmentCount;

    uint terminalSegmentCount;
    uint flags;
    uint pageMapBase; // absolute index into GroupPageMap buffer
    uint pageCount;   // number of pages for this group
};

static const uint CLOD_GROUP_FLAG_IS_VOXEL = 1u << 0;

#endif // CLOD_STRUCTS_HLSLI