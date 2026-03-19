#ifndef CLOD_STRUCTS_HLSLI
#define CLOD_STRUCTS_HLSLI

// Must match CLOD_PER_REFINED_GROUP_SEGMENTS in ClusterLODShaderTypes.h
// #define CLOD_PER_REFINED_GROUP_SEGMENTS

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

    uint refinedGroupIdOffset;
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
    int parentGroupId; // mesh-local group index of the parent group (-1 for root)
};

static const uint CLOD_GROUP_FLAG_IS_VOXEL = 1u << 0;

static const uint CLOD_REPLAY_RECORD_TYPE_NODE = 0;
static const uint CLOD_REPLAY_RECORD_TYPE_MESHLET = 1;
static const uint CLOD_REPLAY_BUFFER_SIZE_BYTES = 100u * 1024u * 1024u;

struct CLodNodeGroupReplayRecord
{
    uint type;
    uint instanceIndex;
    uint viewId;
    uint nodeOrGroupId;
    uint pad0;
};

struct CLodMeshletReplayRecord
{
    uint type;
    uint instanceIndex;
    uint viewId;
    uint groupId;
    uint localMeshletIndex;       // page-local meshlet index
    uint pageSlabDescriptorIndex; // pre-resolved page slab descriptor
    uint pageSlabByteOffset;      // pre-resolved page slab byte offset
    uint pad;
};

static const uint CLOD_REPLAY_SLOT_STRIDE_BYTES = sizeof(CLodMeshletReplayRecord);
static const uint CLOD_REPLAY_SLOT_CAPACITY = CLOD_REPLAY_BUFFER_SIZE_BYTES / CLOD_REPLAY_SLOT_STRIDE_BYTES;

struct CLodReplayBufferState
{
    uint totalWriteCount;
    uint droppedRecords;
    uint pad0;
    uint pad1;
};

struct CLodViewDepthSRVIndex
{
    uint cameraBufferIndex;
    uint linearDepthSRVIndex;
    uint pad0;
    uint pad1;
};

struct CLodNodeGpuInput
{
    uint entrypointIndex;
    uint numRecords;
    uint64_t recordsAddress;
    uint64_t recordStride;
};

struct CLodMultiNodeGpuInput
{
    uint numNodeInputs;
    uint pad0;
    uint64_t nodeInputsAddress;
    uint64_t nodeInputStride;
};

#endif // CLOD_STRUCTS_HLSLI