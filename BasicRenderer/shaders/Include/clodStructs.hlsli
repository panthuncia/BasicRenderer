#ifndef CLOD_STRUCTS_HLSLI
#define CLOD_STRUCTS_HLSLI

struct MeshInstanceClodOffsets
{
    uint clodMeshMetadataIndex;
    uint pad0;
    uint pad1;
    uint pad2;
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

struct ClusterLODGroupChunk
{
    uint vertexChunkByteOffset;
    uint meshletVerticesBase;
    uint groupVertexCount;
    uint meshletVertexCount;
    uint meshletBase;
    uint meshletCount;
    uint meshletTrianglesByteOffset;
    uint meshletTrianglesByteCount;
    uint meshletBoundsBase;
    uint meshletBoundsCount;

    uint compressedPositionWordsBase;
    uint compressedPositionWordCount;
    uint compressedPositionBitsX;
    uint compressedPositionBitsY;
    uint compressedPositionBitsZ;
    uint compressedPositionQuantExp;
    int compressedPositionMinQx;
    int compressedPositionMinQy;
    int compressedPositionMinQz;

    uint compressedNormalWordsBase;
    uint compressedNormalWordCount;

    uint compressedMeshletVertexWordsBase;
    uint compressedMeshletVertexWordCount;
    uint compressedMeshletVertexBits;
    uint compressedFlags;
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