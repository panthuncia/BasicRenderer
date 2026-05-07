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
    uint lodLevelInfoBase;
    uint lodLevelCount;
    uint maxDepth;
    uint voxelDescriptorIndexBase;
    uint voxelDescriptorIndexCount;
    uint voxelGroupDescriptorBase;
    uint voxelGroupDescriptorCount;
    uint voxelCubeRecordBase;
    uint voxelCubeRecordCount;
    uint voxelAttributeSampleBase;
    uint voxelAttributeSampleCount;
};

struct CLodHierarchyLevelInfo
{
    uint rootNode;
    uint nodeRangeOffset;
    uint nodeRangeCount;
    uint pad0;
};

// GPU-visible page table entry - maps a virtual page ID to a slab + byte offset.
struct PageTableEntry
{
    uint slabIndex;      // Which slab ByteAddressBuffer this page lives in.
    uint slabByteOffset; // Byte offset of the page start within that slab.
};

static const uint CLOD_PAGE_ATTRIBUTE_NORMAL = 1u << 0;
static const uint CLOD_PAGE_ATTRIBUTE_JOINTS = 1u << 1;
static const uint CLOD_PAGE_ATTRIBUTE_WEIGHTS = 1u << 2;
static const uint CLOD_PAGE_ATTRIBUTE_COLOR = 1u << 3;

// Embedded at byte 0 of each page-tile. Simplified header.
// 16 x uint32 = 64 bytes.
struct CLodPageHeader
{
    uint meshletCount;                // [0]
    uint compressedPositionQuantExp;  // [1] mesh-wide quantization exponent
    uint attributeMask;               // [2] page-wide optional non-UV attribute mask
    uint uvSetCount;                  // [3] UV set count packed into this page

    uint descriptorOffset;            // [4] byte offset to CLodMeshletDescriptor array
    uint uvDescriptorOffset;          // [5] byte offset to CLodMeshletUvDescriptor table
    uint positionBitstreamOffset;     // [6] byte offset to position bitstream
    uint normalArrayOffset;           // [7] byte offset to normal array
    uint colorArrayOffset;            // [8] byte offset to RGBA8_UNORM color array per vertex
    uint jointArrayOffset;            // [9] byte offset to two-uint4 joint array per vertex
    uint weightArrayOffset;           // [10] byte offset to two-float4 weight array per vertex
    uint uvBitstreamDirectoryOffset;  // [11] byte offset to UV bitstream offset table
    uint triangleStreamOffset;        // [12] byte offset to triangle byte stream
    uint boneIndexStreamOffset;       // [13] byte offset to page-local bone-index stream
    uint reserved0;
    uint reserved1;
};

// Per-meshlet descriptor. Self-contained: non-UV compression params, bounds, LOD metadata.
// 16 x uint32 = 64 bytes = 4 x Load4.
struct CLodMeshletDescriptor
{
    uint positionBitOffset;           // [0] bit offset into page position bitstream
    uint vertexAttributeOffset;       // [1] element offset into page vertex-attribute arrays
    uint triangleByteOffset;          // [2] byte offset into page triangle stream
    uint boneListOffset;              // [3] uint offset into page bone-index stream

    int  minQx;                       // [4] per-meshlet quantization offset X
    int  minQy;                       // [5] per-meshlet quantization offset Y
    int  minQz;                       // [6] per-meshlet quantization offset Z

    uint bitsAndVertexCount;          // [7] bitsX:8 | bitsY:8 | bitsZ:8 | vertexCount:8
    uint triangleCountAndRefinedGroup; // [8] triangleCount:16 | (refinedGroupId+1):16
    uint boneCount;                   // [9]
    uint reserved2;                   // [10]
    uint reserved3;                   // [11]

    float4 bounds;                    // [12-15] bounding sphere {cx, cy, cz, radius}
};

// Per-(meshlet, uv-set) descriptor. 8 x uint32 = 32 bytes = 2 x Load4.
struct CLodMeshletUvDescriptor
{
    uint uvBitOffset;                 // [0] bit offset into this UV set's page-local bitstream
    float uvMinU;                     // [1]
    float uvMinV;                     // [2]
    float uvScaleU;                   // [3]
    float uvScaleV;                   // [4]
    uint uvBits;                      // [5] bitsU:8 | bitsV:8
    uint reserved0;                   // [6]
    uint reserved1;                   // [7]
};

// Helper functions to unpack CLodMeshletDescriptor fields
uint CLodDescBitsX(CLodMeshletDescriptor desc) { return desc.bitsAndVertexCount & 0xFFu; }
uint CLodDescBitsY(CLodMeshletDescriptor desc) { return (desc.bitsAndVertexCount >> 8u) & 0xFFu; }
uint CLodDescBitsZ(CLodMeshletDescriptor desc) { return (desc.bitsAndVertexCount >> 16u) & 0xFFu; }
uint CLodDescVertexCount(CLodMeshletDescriptor desc) { return (desc.bitsAndVertexCount >> 24u) & 0xFFu; }
uint CLodDescTriangleCount(CLodMeshletDescriptor desc) { return desc.triangleCountAndRefinedGroup & 0xFFFFu; }
int  CLodDescRefinedGroupId(CLodMeshletDescriptor desc) { return (int)(desc.triangleCountAndRefinedGroup >> 16u) - 1; }
uint CLodDescBoneCount(CLodMeshletDescriptor desc) { return desc.boneCount; }
uint CLodUvDescBitsU(CLodMeshletUvDescriptor desc) { return desc.uvBits & 0xFFu; }
uint CLodUvDescBitsV(CLodMeshletUvDescriptor desc) { return (desc.uvBits >> 8u) & 0xFFu; }

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

static const uint CLOD_NODE_INTERNAL = 0u;
static const uint CLOD_NODE_VOXEL_GROUP_LEAF = 1u;
static const uint CLOD_NODE_SEGMENT_LEAF = 2u;

static const uint CLOD_GROUP_FLAG_IS_VOXEL = 1u << 0;

static const uint CLOD_VOXEL_STATIC_BONE_INDEX = 0xFFFFFFFFu;

struct CLodVoxelGroupDescriptor
{
    float4 aabbMinAndVoxelWidth;
    float4 aabbMaxAndError;
    uint firstCube;
    uint cubeCount;
    uint resolution;
    uint flags;
};

struct CLodVoxelCubeRecord
{
    uint cubeCoord; // x:10 | y:10 | z:10 in 4x4x4-cell cube coordinates
    uint dominantBoneIndex;
    uint2 occupancyMask;
    float opacitySum;
    uint firstAttribute;
};

struct CLodVoxelAttributeSample
{
    float4 normalAndOpacity;
};

struct CLodVoxelRasterQueueDescriptors
{
    uint workRecordsUAVDescriptorIndex;
    uint workRecordCounterUAVDescriptorIndex;
    uint workRecordCapacity;
    uint pad0;
};

struct CLodVoxelRasterWorkRecord
{
    uint visibleClusterIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};

struct CLodVoxelRasterDispatchCommand
{
    uint dispatchX;
    uint dispatchY;
    uint dispatchZ;
};

static const uint CLOD_VOXEL_PAGE_MAGIC = 0x4C435856u;
static const uint CLOD_VOXEL_PAGE_VERSION = 1u;
static const uint CLOD_VOXEL_PAGE_DESCRIPTOR_OFFSET = 64u;
static const uint CLOD_VOXEL_PAGE_CUBE_RECORD_OFFSET = 112u;
static const uint CLOD_VOXEL_CUBE_RECORD_STRIDE = 24u;
static const uint CLOD_VOXEL_ATTRIBUTE_SAMPLE_STRIDE = 16u;

struct CLodVoxelPageHeader
{
    uint magic;
    uint version;
    uint firstCube;
    uint cubeCount;
    uint descriptorOffset;
    uint cubeRecordsOffset;
    uint attributeSamplesOffset;
    uint attributeSamplesPerCube;
};

GroupPageMapEntry CLodLoadVoxelPageMapEntry(CLodMeshMetadata metadata, ClusterLODGroup group, uint pageIndex)
{
    StructuredBuffer<GroupPageMapEntry> pageMap = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::GroupPageMap)];
    return pageMap[metadata.pageMapBase + group.pageMapBase + pageIndex];
}

CLodVoxelPageHeader CLodLoadVoxelPageHeader(uint slabDescriptorIndex, uint pageByteOffset)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[NonUniformResourceIndex(slabDescriptorIndex)];
    uint4 d0 = slab.Load4(pageByteOffset + 0u);
    uint4 d1 = slab.Load4(pageByteOffset + 16u);
    CLodVoxelPageHeader header;
    header.magic = d0.x;
    header.version = d0.y;
    header.firstCube = d0.z;
    header.cubeCount = d0.w;
    header.descriptorOffset = d1.x;
    header.cubeRecordsOffset = d1.y;
    header.attributeSamplesOffset = d1.z;
    header.attributeSamplesPerCube = d1.w;
    return header;
}

CLodVoxelGroupDescriptor CLodLoadVoxelDescriptorFromPage(uint slabDescriptorIndex, uint pageByteOffset, uint descriptorOffset)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[NonUniformResourceIndex(slabDescriptorIndex)];
    uint addr = pageByteOffset + descriptorOffset;
    uint4 d0 = slab.Load4(addr + 0u);
    uint4 d1 = slab.Load4(addr + 16u);
    uint4 d2 = slab.Load4(addr + 32u);
    CLodVoxelGroupDescriptor descriptor;
    descriptor.aabbMinAndVoxelWidth = asfloat(d0);
    descriptor.aabbMaxAndError = asfloat(d1);
    descriptor.firstCube = d2.x;
    descriptor.cubeCount = d2.y;
    descriptor.resolution = d2.z;
    descriptor.flags = d2.w;
    return descriptor;
}

CLodVoxelCubeRecord CLodLoadVoxelCubeFromPage(uint slabDescriptorIndex, uint pageByteOffset, uint cubeRecordsOffset, uint pageLocalCubeIndex)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[NonUniformResourceIndex(slabDescriptorIndex)];
    uint addr = pageByteOffset + cubeRecordsOffset + pageLocalCubeIndex * CLOD_VOXEL_CUBE_RECORD_STRIDE;
    uint4 d0 = slab.Load4(addr + 0u);
    uint2 d1 = slab.Load2(addr + 16u);
    CLodVoxelCubeRecord cube;
    cube.cubeCoord = d0.x;
    cube.dominantBoneIndex = d0.y;
    cube.occupancyMask = d0.zw;
    cube.opacitySum = asfloat(d1.x);
    cube.firstAttribute = d1.y;
    return cube;
}

bool CLodTryFindVoxelPage(
    CLodMeshMetadata metadata,
    ClusterLODGroup group,
    uint localCubeIndex,
    out GroupPageMapEntry pageEntry,
    out CLodVoxelPageHeader pageHeader,
    out uint pageLocalCubeIndex)
{
    pageEntry = (GroupPageMapEntry)0;
    pageHeader = (CLodVoxelPageHeader)0;
    pageLocalCubeIndex = 0u;

    for (uint pageIndex = 0u; pageIndex < group.pageCount; ++pageIndex)
    {
        GroupPageMapEntry candidateEntry = CLodLoadVoxelPageMapEntry(metadata, group, pageIndex);
        if (candidateEntry.slabDescriptorIndex == 0u && candidateEntry.slabByteOffset == 0u)
        {
            continue;
        }

        CLodVoxelPageHeader candidateHeader = CLodLoadVoxelPageHeader(candidateEntry.slabDescriptorIndex, candidateEntry.slabByteOffset);
        if (candidateHeader.magic != CLOD_VOXEL_PAGE_MAGIC || candidateHeader.version != CLOD_VOXEL_PAGE_VERSION)
        {
            continue;
        }

        if (localCubeIndex >= candidateHeader.firstCube && localCubeIndex < candidateHeader.firstCube + candidateHeader.cubeCount)
        {
            pageEntry = candidateEntry;
            pageHeader = candidateHeader;
            pageLocalCubeIndex = localCubeIndex - candidateHeader.firstCube;
            return true;
        }
    }

    return false;
}

bool CLodTryLoadVoxelGroupDescriptor(
    CLodMeshMetadata metadata,
    uint localGroupId,
    out CLodVoxelGroupDescriptor descriptor)
{
    descriptor = (CLodVoxelGroupDescriptor)0;
    if (localGroupId >= metadata.voxelDescriptorIndexCount || metadata.voxelGroupDescriptorCount == 0u)
    {
        return false;
    }

    StructuredBuffer<ClusterLODGroup> groups = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
    const ClusterLODGroup group = groups[metadata.groupsBase + localGroupId];
    if (group.pageCount > 0u)
    {
        for (uint pageIndex = 0u; pageIndex < group.pageCount; ++pageIndex)
        {
            GroupPageMapEntry pageEntry = CLodLoadVoxelPageMapEntry(metadata, group, pageIndex);
            if (pageEntry.slabDescriptorIndex == 0u && pageEntry.slabByteOffset == 0u)
            {
                continue;
            }

            CLodVoxelPageHeader pageHeader = CLodLoadVoxelPageHeader(pageEntry.slabDescriptorIndex, pageEntry.slabByteOffset);
            if (pageHeader.magic != CLOD_VOXEL_PAGE_MAGIC || pageHeader.version != CLOD_VOXEL_PAGE_VERSION)
            {
                continue;
            }

            descriptor = CLodLoadVoxelDescriptorFromPage(pageEntry.slabDescriptorIndex, pageEntry.slabByteOffset, pageHeader.descriptorOffset);
            return descriptor.cubeCount > 0u;
        }

        return false;
    }

    StructuredBuffer<int> descriptorIndices = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::VoxelDescriptorIndices)];
    const int localDescriptorIndex = descriptorIndices[metadata.voxelDescriptorIndexBase + localGroupId];
    if (localDescriptorIndex < 0 || (uint)localDescriptorIndex >= metadata.voxelGroupDescriptorCount)
    {
        return false;
    }

    StructuredBuffer<CLodVoxelGroupDescriptor> descriptors = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::VoxelGroupDescriptors)];
    descriptor = descriptors[metadata.voxelGroupDescriptorBase + (uint)localDescriptorIndex];
    return descriptor.cubeCount > 0u;
}

CLodVoxelCubeRecord CLodLoadVoxelCube(CLodMeshMetadata metadata, CLodVoxelGroupDescriptor descriptor, uint localGroupId, uint localCubeIndex)
{
    StructuredBuffer<ClusterLODGroup> groups = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
    const ClusterLODGroup group = groups[metadata.groupsBase + localGroupId];
    if (group.pageCount > 0u)
    {
        GroupPageMapEntry pageEntry;
        CLodVoxelPageHeader pageHeader;
        uint pageLocalCubeIndex;
        if (CLodTryFindVoxelPage(metadata, group, localCubeIndex, pageEntry, pageHeader, pageLocalCubeIndex))
        {
            return CLodLoadVoxelCubeFromPage(pageEntry.slabDescriptorIndex, pageEntry.slabByteOffset, pageHeader.cubeRecordsOffset, pageLocalCubeIndex);
        }

        return (CLodVoxelCubeRecord)0;
    }

    StructuredBuffer<CLodVoxelCubeRecord> cubes = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::VoxelCubeRecords)];
    return cubes[metadata.voxelCubeRecordBase + descriptor.firstCube + localCubeIndex];
}

CLodVoxelAttributeSample CLodLoadVoxelAttributeSample(CLodMeshMetadata metadata, CLodVoxelCubeRecord cube, uint localGroupId, uint localCubeIndex, uint localCellIndex)
{
    StructuredBuffer<ClusterLODGroup> groups = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
    const ClusterLODGroup group = groups[metadata.groupsBase + localGroupId];
    if (group.pageCount > 0u)
    {
        GroupPageMapEntry pageEntry;
        CLodVoxelPageHeader pageHeader;
        uint pageLocalCubeIndex;
        if (CLodTryFindVoxelPage(metadata, group, localCubeIndex, pageEntry, pageHeader, pageLocalCubeIndex))
        {
            ByteAddressBuffer slab = ResourceDescriptorHeap[NonUniformResourceIndex(pageEntry.slabDescriptorIndex)];
            const uint attributeIndex = cube.firstAttribute + localCellIndex;
            const uint addr = pageEntry.slabByteOffset + pageHeader.attributeSamplesOffset + attributeIndex * CLOD_VOXEL_ATTRIBUTE_SAMPLE_STRIDE;
            CLodVoxelAttributeSample sample;
            sample.normalAndOpacity = asfloat(slab.Load4(addr));
            return sample;
        }

        return (CLodVoxelAttributeSample)0;
    }

    StructuredBuffer<CLodVoxelAttributeSample> samples = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::VoxelAttributeSamples)];
    return samples[metadata.voxelAttributeSampleBase + cube.firstAttribute + localCellIndex];
}

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

struct CLodDenseClusterWorkRecord
{
    uint instanceIndex;
    uint viewId;
    uint groupIdPacked;
    uint localMeshletIndex;
    uint pageSlabDescriptorIndex;
    uint pageSlabByteOffset;
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

// ---------------------------------------------------------------------------
// Page-job VSM software rasterization records.
// Three-node pipeline: ClusterCull → PageJobBuild → PageJobExpand → PageJobRasterPage.
// ---------------------------------------------------------------------------
#define PAGEJOB_BUILD_THREADS            128
#define PAGEJOB_BUILD_MAX_CLUSTERS       8
#define PAGEJOB_EXPAND_THREADS           64
#define PAGEJOB_RASTER_PAGE_THREADS      128
#define PAGEJOB_TILE_PAGES_X             8
#define PAGEJOB_TILE_PAGES_Y             8
#define PAGEJOB_MAX_TILE_JOBS_PER_CLUSTER 256
#define PAGEJOB_MAX_PAGES_PER_TILE       (PAGEJOB_TILE_PAGES_X * PAGEJOB_TILE_PAGES_Y)

// Build-stage input: batch of cluster indices (same shape as SWRasterBatchRecord).
struct PageJobBuildBatchRecord
{
    uint3 dispatchGrid : SV_DispatchGrid; // (1, 1, 1) — single group per batch
    uint numClusters;                       // 1..PAGEJOB_BUILD_MAX_CLUSTERS
    uint clusterIndices[PAGEJOB_BUILD_MAX_CLUSTERS];
};

// Expand-stage input: one (cluster, page-tile) pair. Build emits these.
struct PageJobExpandRecord
{
    uint3 dispatchGrid : SV_DispatchGrid; // (1, 1, 1)
    uint clusterIndex;                      // visible-cluster-buffer index
    uint packedTileAndClipmap;              // tileMinPageX:8 | tileMinPageY:8 | clipmapLayer:5
    uint packedPageBounds;                  // minPageX:8 | minPageY:8 | maxPageX:8 | maxPageY:8
};

// Raster-page-stage input: one (cluster, physical-page) pair. Expand emits these.
struct PageJobRasterPageRecord
{
    uint3 dispatchGrid : SV_DispatchGrid; // (1, 1, 1)
    uint clusterIndex;                      // visible-cluster-buffer index
    uint physicalPageIndex;                 // physical atlas page index
    uint packedPagePixelOriginAndClipmap;   // pagePixelMinX:16 | pagePixelMinY:16 (absolute virtual pixels)
    uint packedAtlasOriginAndClipmap;       // atlasBaseX:16 | atlasBaseY:16
    uint clipmapLayer;                      // clipmap layer for content-valid write-back
    uint2 wrappedPageCoords;                // for page-table content-valid write-back
};

#endif // CLOD_STRUCTS_HLSLI
