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

// Embedded at byte 0 of each page-tile. Simplified header.
// 16 x uint32 = 64 bytes.
struct CLodPageHeader
{
    uint meshletCount;                // [0]
    uint compressedPositionQuantExp;  // [1] mesh-wide quantization exponent
    uint descriptorOffset;            // [2] byte offset to CLodMeshletDescriptor array
    uint positionBitstreamOffset;     // [3] byte offset to position bitstream

    uint normalArrayOffset;           // [4] byte offset to normal array
    uint triangleStreamOffset;        // [5] byte offset to triangle byte stream
    uint reserved0;
    uint reserved1;
    uint reserved2;
    uint reserved3;
    uint reserved4;
    uint reserved5;
    uint reserved6;
    uint reserved7;
    uint reserved8;
    uint reserved9;
};

// Per-meshlet descriptor. Self-contained: compression params, bounds, LOD metadata.
// 12 x uint32 = 48 bytes = 3 x Load4.
struct CLodMeshletDescriptor
{
    uint positionBitOffset;           // [0] bit offset into page position bitstream
    uint normalWordOffset;            // [1] word offset into page normal array
    uint triangleByteOffset;          // [2] byte offset into page triangle stream

    int  minQx;                       // [3] per-meshlet quantization offset X
    int  minQy;                       // [4] per-meshlet quantization offset Y
    int  minQz;                       // [5] per-meshlet quantization offset Z

    uint bitsAndVertexCount;          // [6] bitsX:8 | bitsY:8 | bitsZ:8 | vertexCount:8
    uint triangleCountAndRefinedGroup; // [7] triangleCount:16 | (refinedGroupId+1):16

    float4 bounds;                    // [8-11] bounding sphere {cx, cy, cz, radius}
};

// Helper functions to unpack CLodMeshletDescriptor fields
uint CLodDescBitsX(CLodMeshletDescriptor desc) { return desc.bitsAndVertexCount & 0xFFu; }
uint CLodDescBitsY(CLodMeshletDescriptor desc) { return (desc.bitsAndVertexCount >> 8u) & 0xFFu; }
uint CLodDescBitsZ(CLodMeshletDescriptor desc) { return (desc.bitsAndVertexCount >> 16u) & 0xFFu; }
uint CLodDescVertexCount(CLodMeshletDescriptor desc) { return (desc.bitsAndVertexCount >> 24u) & 0xFFu; }
uint CLodDescTriangleCount(CLodMeshletDescriptor desc) { return desc.triangleCountAndRefinedGroup & 0xFFFFu; }
int  CLodDescRefinedGroupId(CLodMeshletDescriptor desc) { return (int)(desc.triangleCountAndRefinedGroup >> 16u) - 1; }

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

// Replay buffer: single physical buffer split into two regions.
// Node region stores TraverseNodeRecord (12 bytes), meshlet region stores MeshletBucketRecord (24 bytes).
static const uint CLOD_REPLAY_BUFFER_SIZE_BYTES       = 100u * 1024u * 1024u;
static const uint CLOD_REPLAY_NODE_REGION_SIZE_BYTES   = CLOD_REPLAY_BUFFER_SIZE_BYTES / 2;
static const uint CLOD_REPLAY_MESHLET_REGION_OFFSET    = CLOD_REPLAY_NODE_REGION_SIZE_BYTES;
static const uint CLOD_REPLAY_MESHLET_REGION_SIZE_BYTES = CLOD_REPLAY_BUFFER_SIZE_BYTES - CLOD_REPLAY_NODE_REGION_SIZE_BYTES;

static const uint CLOD_NODE_REPLAY_STRIDE_BYTES    = 12u;  // 3 uints (TraverseNodeRecord)
static const uint CLOD_MESHLET_REPLAY_STRIDE_BYTES = 24u;  // 6 uints (MeshletBucketRecord)

static const uint CLOD_NODE_REPLAY_CAPACITY    = CLOD_REPLAY_NODE_REGION_SIZE_BYTES / CLOD_NODE_REPLAY_STRIDE_BYTES;
static const uint CLOD_MESHLET_REPLAY_CAPACITY = CLOD_REPLAY_MESHLET_REGION_SIZE_BYTES / CLOD_MESHLET_REPLAY_STRIDE_BYTES;

struct CLodReplayBufferState
{
    uint nodeWriteCount;
    uint meshletWriteCount;
    uint nodeDropped;
    uint meshletDropped;
    uint visibleClusterCombinedCount;
    uint pad0;
    uint pad1;
    uint pad2;
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

// Shared software-raster launch constants.
// Both compute and work-graph paths use one 128-thread group per cluster.
#define SW_RASTER_THREADS            128
#define SW_RASTER_GROUPS_PER_CLUSTER 1
#define SW_RASTER_MAX_VERTS          128

// Batched work graph record for software rasterization of small clusters.
// Broadcasting node: ClusterCull accumulates up to SW_BATCH_MAX_CLUSTERS
// cluster indices per record. SWRaster reads full VisibleCluster data from
// the visible clusters buffer via indirection.
#define SW_BATCH_MAX_CLUSTERS 8

struct SWRasterBatchRecord
{
    uint3 dispatchGrid : SV_DispatchGrid; // (numClusters, 1, 1)
    uint numClusters;                       // 1..SW_BATCH_MAX_CLUSTERS
    uint clusterIndices[SW_BATCH_MAX_CLUSTERS]; // unsorted visible cluster buffer indices
};

#endif // CLOD_STRUCTS_HLSLI