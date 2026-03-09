#ifndef CLOD_STRUCTS_HLSLI
#define CLOD_STRUCTS_HLSLI

struct MeshInstanceClodOffsets
{
    uint clodMeshMetadataIndex;
};

struct CLodMeshMetadata
{
    uint groupsBase;
    uint childrenBase;
    uint lodNodesBase;
    uint rootNode; // node index (relative to lodNodesBase) to start traversal from
    uint groupChunkTableBase;
    uint groupChunkTableCount;
};

/// GPU-visible page table entry — maps a virtual page ID to a slab + byte offset.
struct PageTableEntry
{
    uint slabIndex;      // Which slab ByteAddressBuffer this page lives in.
    uint slabByteOffset; // Byte offset of the page start within that slab.
};

struct ClusterLODGroupChunk
{
    // Group metadata
    uint groupVertexCount;
    uint meshletVertexCount;
    uint meshletCount;
    uint meshletTrianglesByteCount;
    uint meshletBoundsCount;

    // Compressed group-local position stream (u32 bitstream words)
    uint compressedPositionWordCount;
    uint compressedPositionBitsX;
    uint compressedPositionBitsY;
    uint compressedPositionBitsZ;
    uint compressedPositionQuantExp;
    int compressedPositionMinQx;
    int compressedPositionMinQy;
    int compressedPositionMinQz;

    // Compressed group-local normal stream (oct-encoded snorm16x2 packed into u32)
    uint compressedNormalWordCount;

    // Compressed group-local meshlet vertex index stream (u32 bitstream words)
    uint compressedMeshletVertexWordCount;
    uint compressedMeshletVertexBits;
    uint compressedFlags;

    // Page-pool fields
    // All 8 data streams live in a unified slab ByteAddressBuffer
    // addressed via the fields below.
    uint pagePoolSlabDescriptorIndex; // Descriptor-heap index of the slab BAB
    uint pagePoolSlabByteOffset;      // Byte offset of allocation start in slab
    uint vertexIntraPageByteOffset;
    uint meshletVertexIntraPageByteOffset;
    uint triangleIntraPageByteOffset;
    uint compPosIntraPageByteOffset;
    uint compNormIntraPageByteOffset;
    uint compMeshletVertIntraPageByteOffset;
    uint meshletIntraPageByteOffset;
    uint boundsIntraPageByteOffset;
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

struct ClusterLODChild
{
    int refinedGroup; // -1 => terminal meshlets bucket
    uint firstLocalMeshletIndex; // group-local contiguous start meshlet index
    uint localMeshletCount;
    uint pad0;
};

struct ClusterLODGroup
{
    ClodBounds bounds; // center/radius/error
    
    uint firstMeshlet;
    uint meshletCount;
    int depth;

    uint firstGroupVertex;
    uint groupVertexCount;
    uint firstChild;
    uint childCount;

    uint terminalChildCount;
    uint flags;
    uint pad0[2];
};

static const uint CLOD_GROUP_FLAG_IS_VOXEL = 1u << 0;

#endif // CLOD_STRUCTS_HLSLI