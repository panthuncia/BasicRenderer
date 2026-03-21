// Compile with DXC target: lib_6_8 (Shader Model 6.8)
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/indirectCommands.hlsli"
#include "include/waveIntrinsicsHelpers.hlsli"
#include "include/occlusionCulling.hlsli"
#include "PerPassRootConstants/clodRootConstants.h"
#include "Include/clodStructs.hlsli"
#include "Include/clodPageAccess.hlsli"

// meshopt_Meshlet layout on GPU
struct Meshlet
{
    uint vertex_offset;
    uint triangle_offset;
    uint vertex_count;
    uint triangle_count;
};

struct ClusterLODNodeRange
{
    uint isGroup; // 0=internal, 2=segment-leaf
    uint indexOrOffset; // segment-leaf: mesh-local segment index
                         // internal: childOffset (relative to lodNodesBase)
    uint countMinusOne; // internal: childCountMinusOne; leaf: unused
    uint ownerGroupId;  // segment-leaf: mesh-local group index (for page resolution + streaming)
};

struct ClusterLODTraversalMetric
{
    float4 centerAndRadius; // xyz center (mesh space), w radius (mesh space)
    float maxQuadricError; // mesh-space conservative error bound for this subtree/leaf
    float pad0[3];
};

struct ClusterLODNode
{
    ClusterLODNodeRange range;
    ClusterLODTraversalMetric metric;
};

static const uint WG_COUNTER_OBJECT_CULL_THREADS = 0;
static const uint WG_COUNTER_OBJECT_CULL_IN_RANGE_THREADS = 1;
static const uint WG_COUNTER_OBJECT_CULL_VISIBLE_THREADS = 2;
static const uint WG_COUNTER_OBJECT_CULL_TRAVERSE_RECORDS = 3;

static const uint WG_COUNTER_TRAVERSE_THREADS = 4;
static const uint WG_COUNTER_TRAVERSE_INTERNAL_NODE_RECORDS = 5;
static const uint WG_COUNTER_TRAVERSE_LEAF_NODE_RECORDS = 6;
static const uint WG_COUNTER_TRAVERSE_CULLED_NODE_RECORDS = 7;
static const uint WG_COUNTER_TRAVERSE_REJECTED_BY_ERROR_RECORDS = 8;
static const uint WG_COUNTER_TRAVERSE_ACTIVE_CHILD_THREADS = 9;
static const uint WG_COUNTER_TRAVERSE_TRAVERSE_RECORDS = 10;

static const uint WG_COUNTER_CLUSTER_CULL_THREADS = 11;
static const uint WG_COUNTER_CLUSTER_CULL_IN_RANGE_THREADS = 12;
static const uint WG_COUNTER_CLUSTER_CULL_WAVES = 13;
static const uint WG_COUNTER_CLUSTER_CULL_ACTIVE_LANES = 14;
static const uint WG_COUNTER_CLUSTER_CULL_SURVIVING_LANES = 15;
static const uint WG_COUNTER_CLUSTER_CULL_ZERO_SURVIVOR_WAVES = 16;
static const uint WG_COUNTER_CLUSTER_CULL_VISIBLE_CLUSTER_WRITES = 17;

static const uint WG_COUNTER_TRAVERSE_COALESCED_LAUNCHES = 18;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_RECORDS = 19;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_1 = 20;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_2 = 21;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_3 = 22;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_4 = 23;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_5 = 24;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_6 = 25;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_7 = 26;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_8 = 27;

static const uint WG_COUNTER_PHASE1_OCCLUSION_NODE_REPLAY_ENQUEUE_ATTEMPTS = 28;
static const uint WG_COUNTER_PHASE1_OCCLUSION_CLUSTER_REPLAY_ENQUEUE_ATTEMPTS = 29;

static const uint WG_COUNTER_PHASE2_REPLAY_NODE_LAUNCHES = 30;
static const uint WG_COUNTER_PHASE2_REPLAY_NODE_INPUT_RECORDS = 31;
static const uint WG_COUNTER_PHASE2_REPLAY_NODE_RECORDS_EMITTED = 32;

static const uint WG_COUNTER_PHASE2_REPLAY_MESHLET_LAUNCHES = 33;
static const uint WG_COUNTER_PHASE2_REPLAY_MESHLET_INPUT_RECORDS = 34;
static const uint WG_COUNTER_PHASE2_REPLAY_MESHLET_BUCKET_RECORDS_EMITTED = 35;

static const uint WG_COUNTER_PHASE2_REPLAY_TRAVERSE_RECORDS_CONSUMED = 36;
static const uint WG_COUNTER_PHASE2_REPLAY_CLUSTER_BUCKET_RECORDS_CONSUMED = 37;

static const uint WG_COUNTER_TRAVERSE_SEGMENT_RECORDS = 38;

static const uint WG_COUNTER_SEGMENT_EVALUATE_THREADS = 39;
static const uint WG_COUNTER_SEGMENT_EVALUATE_SEGMENT_RECORDS = 40;
static const uint WG_COUNTER_SEGMENT_EVALUATE_EMIT_BUCKET_THREADS = 41;
static const uint WG_COUNTER_SEGMENT_EVALUATE_REFINED_TRAVERSAL_THREADS = 42;
static const uint WG_COUNTER_SEGMENT_EVALUATE_NON_RESIDENT_REFINED_CHILD_THREADS = 43;
static const uint WG_COUNTER_SEGMENT_EVALUATE_COALESCED_LAUNCHES = 44;
static const uint WG_COUNTER_SEGMENT_EVALUATE_COALESCED_INPUT_RECORDS = 45;

static const uint WG_COUNTER_CLUSTER_CULL_MESHLET_ITERATIONS = 46;
static const uint WG_COUNTER_CLUSTER_CULL_REJECTED_FRUSTUM = 47;
static const uint WG_COUNTER_CLUSTER_CULL_REJECTED_CONDITION2 = 48;
static const uint WG_COUNTER_CLUSTER_CULL_REJECTED_OCCLUSION = 49;
static const uint WG_COUNTER_CLUSTER_CULL_REJECTED_OUT_OF_RANGE = 50;
static const uint WG_COUNTER_CLUSTER_CULL_REJECTED_PAGE_BOUNDS = 51;

static const uint CLOD_STREAM_REQUEST_CAPACITY = (1u << 16);
static const uint CLOD_USED_GROUPS_CAPACITY = (1u << 17);
static const uint CLOD_STREAM_VIEWID_MASK = 0xFFFFu;
static const uint CLOD_STREAM_PRIORITY_SHIFT = 16u;

static const uint CLOD_RECORD_SOURCE_PASS1 = 0;
static const uint CLOD_RECORD_SOURCE_REPLAY = 1;

bool CLodWorkGraphTelemetryEnabled()
{
    return (CLOD_WORKGRAPH_FLAGS & CLOD_WORKGRAPH_FLAG_TELEMETRY_ENABLED) != 0u;
}

bool CLodWorkGraphOcclusionEnabled()
{
    return (CLOD_WORKGRAPH_FLAGS & CLOD_WORKGRAPH_FLAG_OCCLUSION_ENABLED) != 0u;
}

uint CLodBitMask(uint key)
{
    return 1u << (key & 31u);
}

uint CLodBitWordAddress(uint key)
{
    return (key >> 5u) * 4u;
}

bool CLodReadBit(ByteAddressBuffer bits, uint key)
{
    const uint packed = bits.Load(CLodBitWordAddress(key));
    return (packed & CLodBitMask(key)) != 0u;
}

bool CLodTrySetBit(RWByteAddressBuffer bits, uint key)
{
    uint oldPacked = 0;
    bits.InterlockedOr(CLodBitWordAddress(key), CLodBitMask(key), oldPacked);
    return (oldPacked & CLodBitMask(key)) == 0u;
}

uint CLodPackViewPriority(uint viewId, float fallbackErrorOverDistance)
{
    const float clampedPriority = min(max(fallbackErrorOverDistance * 1024.0f, 0.0f), 65535.0f);
    const uint quantizedPriority = (uint)(clampedPriority + 0.5f);
    return ((quantizedPriority & CLOD_STREAM_VIEWID_MASK) << CLOD_STREAM_PRIORITY_SHIFT)
        | (viewId & CLOD_STREAM_VIEWID_MASK);
}

static const uint TRAVERSE_THREADS_PER_GROUP = 32;
static const uint BVH_MAX_CHILDREN = 8;
static const uint COALESCED_INPUT_COUNT_HISTOGRAM_BUCKETS = 8;

static const uint TRAVERSE_RECORDS_PER_GROUP = TRAVERSE_THREADS_PER_GROUP;
static const uint SEGMENT_EVALUATE_THREADS_PER_GROUP = 32;
static const uint SEGMENT_EVALUATE_RECORDS_PER_GROUP = SEGMENT_EVALUATE_THREADS_PER_GROUP;
static const uint MAX_RECORDS_PER_SEGMENT = 8;

void WGTelemetryAdd(uint counterIndex, uint value)
{
    if (!CLodWorkGraphTelemetryEnabled())
    {
        return;
    }

    RWStructuredBuffer<uint> telemetryCounters = ResourceDescriptorHeap[CLOD_WORKGRAPH_TELEMETRY_DESCRIPTOR_INDEX];
    InterlockedAdd(telemetryCounters[counterIndex], value);
}

void ReplayReserveSlotsWave(
    RWStructuredBuffer<CLodReplayBufferState> replayState,
    uint capacity,
    out uint slot,
    out bool valid)
{
    const uint4 activeMask = WaveActiveBallot(true);
    const uint activeCount = CountBits128(activeMask);
    const uint leaderLane = WaveFirstLaneFromMask(activeMask);
    const uint laneRank = GetLaneRankInGroup(activeMask, WaveGetLaneIndex());

    uint baseSlot = 0;
    if (WaveGetLaneIndex() == leaderLane) {
        InterlockedAdd(replayState[0].totalWriteCount, activeCount, baseSlot);
    }

    baseSlot = WaveReadLaneAt(baseSlot, leaderLane);
    slot = baseSlot + laneRank;
    valid = slot < capacity;

    const uint droppedCount = CountBits128(WaveActiveBallot(!valid));
    if (WaveGetLaneIndex() == leaderLane && droppedCount > 0) {
        InterlockedAdd(replayState[0].droppedRecords, droppedCount);
    }
}

bool ReplayTryAppendNode(uint instanceIndex, uint viewId, uint nodeId)
{
    WGTelemetryAdd(WG_COUNTER_PHASE1_OCCLUSION_NODE_REPLAY_ENQUEUE_ATTEMPTS, 1);

    RWByteAddressBuffer replayBuffer = ResourceDescriptorHeap[CLOD_OCCLUSION_REPLAY_BUFFER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReplayBufferState> replayState = ResourceDescriptorHeap[CLOD_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX];

    const uint recordSize = CLOD_REPLAY_SLOT_STRIDE_BYTES;
    const uint capacity = CLOD_REPLAY_SLOT_CAPACITY;

    uint slot = 0;
    bool valid = false;
    ReplayReserveSlotsWave(replayState, capacity, slot, valid);

    if (!valid) {
        return false;
    }

    const uint byteOffset = slot * recordSize;
    replayBuffer.Store4(byteOffset, uint4(CLOD_REPLAY_RECORD_TYPE_NODE, instanceIndex, viewId, nodeId));
    replayBuffer.Store(byteOffset + 16u, 0u);
    return true;
}

bool ReplayTryAppendMeshlet(uint instanceIndex, uint viewId, uint groupId, uint localMeshletIndex,
                            uint pageSlabDescriptorIndex, uint pageSlabByteOffset)
{
    WGTelemetryAdd(WG_COUNTER_PHASE1_OCCLUSION_CLUSTER_REPLAY_ENQUEUE_ATTEMPTS, 1);

    RWByteAddressBuffer replayBuffer = ResourceDescriptorHeap[CLOD_OCCLUSION_REPLAY_BUFFER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReplayBufferState> replayState = ResourceDescriptorHeap[CLOD_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX];

    const uint recordSize = CLOD_REPLAY_SLOT_STRIDE_BYTES;
    const uint capacity = CLOD_REPLAY_SLOT_CAPACITY;

    uint slot = 0;
    bool valid = false;
    ReplayReserveSlotsWave(replayState, capacity, slot, valid);

    if (!valid) {
        return false;
    }

    const uint byteOffset = slot * recordSize;
    replayBuffer.Store4(byteOffset, uint4(CLOD_REPLAY_RECORD_TYPE_MESHLET, instanceIndex, viewId, groupId));
    replayBuffer.Store4(byteOffset + 16u, uint4(localMeshletIndex, pageSlabDescriptorIndex, pageSlabByteOffset, 0u));
    return true;
}

// ----- records -----
struct ObjectCullRecord
{
    uint viewDataIndex; // One record per view, times...
    uint activeDrawSetIndicesSRVIndex; // One record per draw set
    uint activeDrawCount;
    uint3 dispatchGrid : SV_DispatchGrid; // Drives dispatch size
};

struct TraverseNodeRecord
{
    uint instanceIndex;
    uint nodeId;
    uint viewId;
    uint sourceTag;
    uint allowRefine;
};

struct SegmentEvalRecord
{
    uint instanceIndex;
    uint segmentIndex;   // mesh-local segment index
    uint ownerGroupId;   // mesh-local group index (for page resolution + streaming)
    uint viewId;
    uint sourceTag;
    uint allowRefine;
};

struct MeshletBucketRecord
{
    uint instanceIndex;
    uint viewId;

    uint groupId;
    uint childFirstLocalMeshletIndex; // page-local
    uint childLocalMeshletCount;
    uint sourceTag;
    uint pageSlabDescriptorIndex;     // pre-resolved page slab descriptor
    uint pageSlabByteOffset;          // pre-resolved page slab byte offset
};

// Phase-2 entry: replay queued node work items.
[Shader("node")]
[NodeID("ReplayNodeGroup")]
[NodeLaunch("coalescing")]
[NumThreads(32, 1, 1)]
[NodeIsProgramEntry]
void WG_ReplayNodeGroup(
    [MaxRecords(32)] GroupNodeInputRecords<CLodNodeGroupReplayRecord> inRecs,
    uint3 gtid : SV_GroupThreadID,
    [MaxRecords(32)] NodeOutput<TraverseNodeRecord> TraverseNodes)
{
    const uint lane = gtid.x;
    const uint inputCount = inRecs.Count();
    const bool inRange = lane < inputCount;

    if (lane == 0) {
        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_NODE_LAUNCHES, 1);
        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_NODE_INPUT_RECORDS, inputCount);
    }

    bool emitTraverse = false;
    TraverseNodeRecord traverseRecord = (TraverseNodeRecord)0;

    if (inRange) {
        const CLodNodeGroupReplayRecord rec = inRecs[lane];
        if (rec.type == CLOD_REPLAY_RECORD_TYPE_NODE) {
            emitTraverse = true;
            traverseRecord.instanceIndex = rec.instanceIndex;
            traverseRecord.nodeId = rec.nodeOrGroupId;
            traverseRecord.viewId = rec.viewId;
            traverseRecord.sourceTag = CLOD_RECORD_SOURCE_REPLAY;
            traverseRecord.allowRefine = 1u;
        }
    }

    ThreadNodeOutputRecords<TraverseNodeRecord> outTraverse =
        TraverseNodes.GetThreadNodeOutputRecords(emitTraverse ? 1 : 0);

    if (emitTraverse) {
        outTraverse.Get() = traverseRecord;
        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_NODE_RECORDS_EMITTED, 1);
    }

    outTraverse.OutputComplete();
}

// Phase-2 entry: replay queued meshlet work items.
[Shader("node")]
[NodeID("ReplayMeshlet")]
[NodeLaunch("coalescing")]
[NumThreads(32, 1, 1)]
[NodeIsProgramEntry]
void WG_ReplayMeshlet(
    [MaxRecords(32)] GroupNodeInputRecords<CLodMeshletReplayRecord> inRecs,
    uint3 gtid : SV_GroupThreadID,
    [MaxRecords(32)] NodeOutput<MeshletBucketRecord> ClusterCull1)
{
    const uint lane = gtid.x;
    const uint inputCount = inRecs.Count();
    bool emitBucket = false;
    CLodMeshletReplayRecord meshletRecord = (CLodMeshletReplayRecord)0;

    if (lane == 0) {
        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_MESHLET_LAUNCHES, 1);
        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_MESHLET_INPUT_RECORDS, inputCount);
    }

    if (lane < inputCount) {
        const CLodMeshletReplayRecord rec = inRecs[lane];
        if (rec.type == CLOD_REPLAY_RECORD_TYPE_MESHLET) {
            emitBucket = true;
            meshletRecord = rec;
        }
    }

    ThreadNodeOutputRecords<MeshletBucketRecord> outBucket =
        ClusterCull1.GetThreadNodeOutputRecords(emitBucket ? 1 : 0);

    if (emitBucket) {
        MeshletBucketRecord bucket = (MeshletBucketRecord)0;
        bucket.instanceIndex = meshletRecord.instanceIndex;
        bucket.viewId = meshletRecord.viewId;
        bucket.groupId = meshletRecord.groupId;
        bucket.childFirstLocalMeshletIndex = meshletRecord.localMeshletIndex;
        bucket.childLocalMeshletCount = 1;
        bucket.sourceTag = CLOD_RECORD_SOURCE_REPLAY;
        bucket.pageSlabDescriptorIndex = meshletRecord.pageSlabDescriptorIndex;
        bucket.pageSlabByteOffset = meshletRecord.pageSlabByteOffset;
        outBucket.Get() = bucket;

        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_MESHLET_BUCKET_RECORDS_EMITTED, 1);
    }

    outBucket.OutputComplete();
}

// struct MeshletWorkRecord
// {
//     uint instanceIndex;
//     uint meshletId;  // absolute meshlet index into the meshlet buffer (after base)
//     uint fixedRasterBucketOffset;
// };

// Conservative max-axis scale from a row-vector local->world
float MaxAxisScale_RowVector(float4x4 M)
{
    float3 ax = float3(M[0][0], M[0][1], M[0][2]);
    float3 ay = float3(M[1][0], M[1][1], M[1][2]);
    float3 az = float3(M[2][0], M[2][1], M[2][2]);
    return max(length(ax), max(length(ay), length(az)));
}

float3 ToViewSpace(float3 objectCenter, row_major matrix objectModelMatrix, row_major matrix viewMatrix)
{
    float4 worldSpaceCenter = mul(float4(objectCenter, 1.0f), objectModelMatrix);
    return mul(worldSpaceCenter, viewMatrix).xyz;
}

bool SphereOutsideFrustumViewSpace(float3 viewSpaceCenter, float radius, Camera camera)
{
    [unroll]
    for (uint i = 0; i < 6; ++i)
    {
        float4 plane = camera.clippingPlanes[i].plane;
        float distanceToPlane = dot(plane.xyz, viewSpaceCenter) + plane.w;
        if (distanceToPlane < -radius)
        {
            return true;
        }
    }

    return false;
}

bool SphereOutsideFrustumViewSpace(float3 viewSpaceCenter, float radius, float4 planes[6])
{
    [unroll]
    for (uint i = 0; i < 6; ++i)
    {
        float distanceToPlane = dot(planes[i].xyz, viewSpaceCenter) + planes[i].w;
        if (distanceToPlane < -radius)
        {
            return true;
        }
    }

    return false;
}

// Node: ObjectCull (entry)
[Shader("node")]
[NodeID("ObjectCull")]
[NodeLaunch("broadcasting")]
[NumThreads(64, 1, 1)]
[NodeMaxDispatchGrid(10000, 1, 1)]
[NodeIsProgramEntry]
void WG_ObjectCull(
    DispatchNodeInputRecord< ObjectCullRecord> inRec,
    const uint3 vGroupThreadID : SV_GroupThreadID,
    const uint3 vDispatchThreadID : SV_DispatchThreadID,
    [MaxRecords(64)] NodeOutput<TraverseNodeRecord> TraverseNodes) {
    const ObjectCullRecord hdr = inRec.Get();
    const bool inRange = (vDispatchThreadID.x < hdr.activeDrawCount);

    WGTelemetryAdd(WG_COUNTER_OBJECT_CULL_THREADS, 1);
    if (inRange) {
        WGTelemetryAdd(WG_COUNTER_OBJECT_CULL_IN_RANGE_THREADS, 1);
    }

    uint outCount = 0;
    TraverseNodeRecord outRecord = (TraverseNodeRecord) 0;

    if (inRange) {
        StructuredBuffer<uint> activeDrawSetIndicesBuffer =
                    ResourceDescriptorHeap[hdr.activeDrawSetIndicesSRVIndex];

        StructuredBuffer<DispatchMeshIndirectCommand> indirectCommandBuffer =
                    ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::IndirectCommandBuffers::Master)];

        const uint drawcallIndex = activeDrawSetIndicesBuffer[vDispatchThreadID.x];
        const uint perMeshInstanceBufferIndex = indirectCommandBuffer[drawcallIndex].perMeshInstanceBufferIndex;

        StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
                    ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
        const PerMeshInstanceBuffer instanceData = perMeshInstanceBuffer[perMeshInstanceBufferIndex];

        StructuredBuffer<PerMeshBuffer> perMeshBuffer =
                    ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
        const PerMeshBuffer perMesh = perMeshBuffer[instanceData.perMeshBufferIndex];

        StructuredBuffer<PerObjectBuffer> perObjectBuffer =
                    ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
        const row_major matrix objectModelMatrix = perObjectBuffer[instanceData.perObjectBufferIndex].model;

        StructuredBuffer<Camera> cameras =
                    ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
        const Camera camera = cameras[hdr.viewDataIndex];

        const float3 objectSpaceCenter = perMesh.boundingSphere.sphere.xyz;
        const float3 viewSpaceCenter = ToViewSpace(objectSpaceCenter, objectModelMatrix, camera.view);
        const float worldRadius = perMesh.boundingSphere.sphere.w * MaxAxisScale_RowVector(objectModelMatrix);

        const bool culled = SphereOutsideFrustumViewSpace(viewSpaceCenter, worldRadius, camera);
        if (!culled) {
            StructuredBuffer<MeshInstanceClodOffsets> meshInstanceClodOffsets =
                            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
            StructuredBuffer<CLodMeshMetadata> clodMeshMetadataBuffer =
                            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::MeshMetadata)];

            const MeshInstanceClodOffsets off = meshInstanceClodOffsets[perMeshInstanceBufferIndex];
            const CLodMeshMetadata clodMeshMetadata = clodMeshMetadataBuffer[off.clodMeshMetadataIndex];

            outRecord.viewId = hdr.viewDataIndex;
            outRecord.instanceIndex =perMeshInstanceBufferIndex;
            outRecord.nodeId = clodMeshMetadata.rootNode;   // BVH root node for this mesh
            outRecord.sourceTag = CLOD_RECORD_SOURCE_PASS1;
            outRecord.allowRefine = 1u;
            outCount = 1;

            WGTelemetryAdd(WG_COUNTER_OBJECT_CULL_VISIBLE_THREADS, 1);
            WGTelemetryAdd(WG_COUNTER_OBJECT_CULL_TRAVERSE_RECORDS, 1);
        }
    }

    // Uniform call; per-thread count may be 0/1.
    ThreadNodeOutputRecords<TraverseNodeRecord> outRecs =
        TraverseNodes.GetThreadNodeOutputRecords(outCount);

    if (outCount == 1) {
        outRecs.Get() = outRecord;
    }

    // Must be uniform even when some threads requested 0 records.
    outRecs.OutputComplete();
}

// NVIDIA-style error-over-distance metric:
// errorOverDistance = (error * scale) / max(dist - radius, znear)
float ErrorOverDistance(float3 worldCenter, float worldRadius, float errorMeshSpace, float errorScale, float3 cameraPos, float zNear) {
    // Conservative "distance to sphere surface"
    float dist = length(worldCenter - cameraPos);
    float denom = max(dist - worldRadius, zNear);

    return (errorMeshSpace * errorScale) / denom;
}

// Node: TraverseNodes (recursive, BVH-only)
[Shader("node")]
[NodeID("TraverseNodes")]
[NodeLaunch("coalescing")]
[NumThreads(TRAVERSE_THREADS_PER_GROUP, 1, 1)]
[NodeMaxRecursionDepth(25)]
void WG_TraverseNodes(
    [MaxRecords(TRAVERSE_RECORDS_PER_GROUP)] GroupNodeInputRecords<TraverseNodeRecord> inRecs,
    uint GI : SV_GroupIndex,
    [MaxRecords(TRAVERSE_RECORDS_PER_GROUP * BVH_MAX_CHILDREN)] NodeOutput<TraverseNodeRecord> TraverseNodes,
    [MaxRecordsSharedWith(TraverseNodes)] NodeOutput<SegmentEvalRecord> SegmentEvaluate)
{
    const uint slot = GI;
    const uint inputCount = inRecs.Count();
    const bool slotActive = slot < inputCount;

    WGTelemetryAdd(WG_COUNTER_TRAVERSE_THREADS, 1);
    if (slot == 0) {
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_COALESCED_LAUNCHES, 1);
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_COALESCED_INPUT_RECORDS, inputCount);
        if (inputCount > 0 && inputCount <= COALESCED_INPUT_COUNT_HISTOGRAM_BUCKETS) {
            WGTelemetryAdd(WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_1 + (inputCount - 1), 1);
        }
    }

    uint emitTraverseCount = 0;
    bool emitSegment = false;
    TraverseNodeRecord childRecords[BVH_MAX_CHILDREN];
    SegmentEvalRecord segmentRecord = (SegmentEvalRecord)0;

    if (slotActive) {
        const TraverseNodeRecord rec = inRecs[slot];
        const bool parentAllowsRefine = (rec.allowRefine != 0u);
        if (rec.sourceTag == CLOD_RECORD_SOURCE_REPLAY) {
            WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_TRAVERSE_RECORDS_CONSUMED, 1);
        }
        const bool replaySource = (rec.sourceTag == CLOD_RECORD_SOURCE_REPLAY);

        StructuredBuffer<MeshInstanceClodOffsets> clodOffsets =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
        StructuredBuffer<CLodMeshMetadata> clodMeshMetadataBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::MeshMetadata)];
        const MeshInstanceClodOffsets off = clodOffsets[rec.instanceIndex];
        const CLodMeshMetadata clodMeshMetadata = clodMeshMetadataBuffer[off.clodMeshMetadataIndex];
        StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
        const uint objectBufferIndex = perMeshInstanceBuffer[rec.instanceIndex].perObjectBufferIndex;
        StructuredBuffer<PerObjectBuffer> perObjectBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
        const row_major matrix objectModelMatrix = perObjectBuffer[objectBufferIndex].model;
        StructuredBuffer<Camera> cameras =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
        const Camera camera = cameras[rec.viewId];
        StructuredBuffer<CullingCameraInfo> cameraInfos =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
        const CullingCameraInfo cam = cameraInfos[rec.viewId];
        StructuredBuffer<ClusterLODGroup> groups =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
        StructuredBuffer<ClusterLODNode> lodNodes =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Nodes)];

        const ClusterLODNode node = lodNodes[clodMeshMetadata.lodNodesBase + rec.nodeId];

        if (node.range.isGroup == 0) {
            WGTelemetryAdd(WG_COUNTER_TRAVERSE_INTERNAL_NODE_RECORDS, 1);
        }
        else {
            WGTelemetryAdd(WG_COUNTER_TRAVERSE_LEAF_NODE_RECORDS, 1);
        }

        const float nodeUniformScale = MaxAxisScale_RowVector(objectModelMatrix);
        const float3 nodeCenterObjectSpace = node.metric.centerAndRadius.xyz;
        const float3 nodeCenterViewSpace = ToViewSpace(nodeCenterObjectSpace, objectModelMatrix, camera.view);
        const float nodeRadiusWorld = node.metric.centerAndRadius.w * nodeUniformScale;
        const bool nodeCulled = !replaySource && SphereOutsideFrustumViewSpace(nodeCenterViewSpace, nodeRadiusWorld, camera);

        if (nodeCulled) {
            WGTelemetryAdd(WG_COUNTER_TRAVERSE_CULLED_NODE_RECORDS, 1);
        }
        else {
            // LOD pre-filter: for segment-leaves, check if the own group's
            // error-over-distance exceeds threshold (condition 1 of the
            // meshoptimizer rendering rule).  Uses the actual group sphere
            // for accuracy; the BVH node sphere is only for frustum culling.
            // For internal nodes, the BVH node sphere and propagated max
            // error provide a conservative bound.

            bool nodeWantsTraversal = false;
            if (node.range.isGroup != 0) {
                const uint groupGlobalIndex = clodMeshMetadata.groupsBase + node.range.ownerGroupId;
                const ClusterLODGroup grp = groups[groupGlobalIndex];

                const float3 grpWorldCenter = mul(float4(grp.bounds.centerAndRadius.xyz, 1.0f), objectModelMatrix).xyz;
                const float grpWorldRadius = grp.bounds.centerAndRadius.w * nodeUniformScale;
                const float grpEOD = ErrorOverDistance(
                    grpWorldCenter, grpWorldRadius,
                    grp.bounds.error, nodeUniformScale,
                    cam.positionWorldSpace.xyz, cam.zNear);
                nodeWantsTraversal = parentAllowsRefine && (grpEOD >= cam.errorOverDistanceThreshold);
            }
            else {
                const float3 lodCheckWorldCenter = mul(float4(nodeCenterObjectSpace, 1.0f), objectModelMatrix).xyz;
                const float lodCheckWorldRadius = nodeRadiusWorld;
                const float nodeErrorOverDistance = ErrorOverDistance(
                    lodCheckWorldCenter,
                    lodCheckWorldRadius,
                    node.metric.maxQuadricError,
                    nodeUniformScale,
                    cam.positionWorldSpace.xyz,
                    cam.zNear);
                nodeWantsTraversal = parentAllowsRefine && (nodeErrorOverDistance >= cam.errorOverDistanceThreshold);
            }

            if (node.range.isGroup != 0) {
                // Segment-leaf: pruned if own group's error is under
                // threshold.  SegmentEvaluate performs the full two-sided
                // check (own group + child group).
                if (!nodeWantsTraversal) {
                    WGTelemetryAdd(WG_COUNTER_TRAVERSE_REJECTED_BY_ERROR_RECORDS, 1);
                }
                else {
                    emitSegment = true;
                    segmentRecord.instanceIndex = rec.instanceIndex;
                    segmentRecord.segmentIndex = node.range.indexOrOffset;
                    segmentRecord.ownerGroupId = node.range.ownerGroupId;
                    segmentRecord.viewId = rec.viewId;
                    segmentRecord.sourceTag = rec.sourceTag;
                    segmentRecord.allowRefine = rec.allowRefine;
                }
            }
            else if (!nodeWantsTraversal) {
                WGTelemetryAdd(WG_COUNTER_TRAVERSE_REJECTED_BY_ERROR_RECORDS, 1);
            }
            else {
                bool occlusionCulled = false;
                if (CLodWorkGraphOcclusionEnabled() && !camera.isOrtho) {
                    StructuredBuffer<CLodViewDepthSRVIndex> viewDepthSRVIndices =
                        ResourceDescriptorHeap[CLOD_VIEW_DEPTH_SRV_INDICES_DESCRIPTOR_INDEX];
                    const uint depthMapDescriptorIndex = viewDepthSRVIndices[rec.viewId].linearDepthSRVIndex;
                    if (depthMapDescriptorIndex != 0) {
                        if (replaySource) {
                            // Phase 2 replay: HZB is from this frame's Phase 1 depth,
                            // so test current-frame bounding spheres.
                            OcclusionCullingPerspectiveTexture2D(
                                occlusionCulled,
                                camera,
                                nodeCenterViewSpace,
                                -nodeCenterViewSpace.z,
                                nodeRadiusWorld,
                                depthMapDescriptorIndex);
                        } else {
                            // Phase 1: HZB is from previous frame's depth,
                            // so reproject bounding sphere into previous frame's camera space.
                            const row_major matrix prevModelMatrix = perObjectBuffer[objectBufferIndex].prevModel;
                            const float prevNodeUniformScale = MaxAxisScale_RowVector(prevModelMatrix);
                            const float3 prevNodeCenterViewSpace = ToViewSpace(nodeCenterObjectSpace, prevModelMatrix, camera.prevView);
                            const float prevNodeRadiusWorld = node.metric.centerAndRadius.w * prevNodeUniformScale;
                            OcclusionCullingPerspectiveTexture2D(
                                occlusionCulled,
                                camera,
                                prevNodeCenterViewSpace,
                                -prevNodeCenterViewSpace.z,
                                prevNodeRadiusWorld,
                                depthMapDescriptorIndex,
                                camera.prevUnjitteredProjection);
                        }
                    }
                }

                if (occlusionCulled) {
                    if (!replaySource) {
                        ReplayTryAppendNode(
                            rec.instanceIndex,
                            rec.viewId,
                            rec.nodeId);
                    }
                }
                else {
                    emitTraverseCount = min(node.range.countMinusOne + 1u, BVH_MAX_CHILDREN);
                    [unroll]
                    for (uint childIndex = 0; childIndex < BVH_MAX_CHILDREN; ++childIndex) {
                        if (childIndex >= emitTraverseCount) {
                            break;
                        }

                        TraverseNodeRecord childRecord = (TraverseNodeRecord)0;
                        childRecord.instanceIndex = rec.instanceIndex;
                        childRecord.viewId = rec.viewId;
                        childRecord.nodeId = node.range.indexOrOffset + childIndex;
                        childRecord.sourceTag = rec.sourceTag;
                        childRecord.allowRefine = nodeWantsTraversal ? 1u : 0u;
                        childRecords[childIndex] = childRecord;
                    }
                }
            }
        }
    }

    ThreadNodeOutputRecords<TraverseNodeRecord> outNodes =
        TraverseNodes.GetThreadNodeOutputRecords(emitTraverseCount);
    ThreadNodeOutputRecords<SegmentEvalRecord> outSegments =
        SegmentEvaluate.GetThreadNodeOutputRecords(emitSegment ? 1 : 0);

    if (emitTraverseCount > 0) {
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_ACTIVE_CHILD_THREADS, emitTraverseCount);
        [unroll]
        for (uint childIndex = 0; childIndex < BVH_MAX_CHILDREN; ++childIndex) {
            if (childIndex >= emitTraverseCount) {
                break;
            }

            outNodes[childIndex] = childRecords[childIndex];
            WGTelemetryAdd(WG_COUNTER_TRAVERSE_TRAVERSE_RECORDS, 1);
        }
    }

    if (emitSegment) {
        outSegments.Get() = segmentRecord;
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_SEGMENT_RECORDS, 1);
    }

    outNodes.OutputComplete();
    outSegments.OutputComplete();
}
#define CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP 32

// Node: SegmentEvaluate
[Shader("node")]
[NodeID("SegmentEvaluate")]
[NodeLaunch("coalescing")]
[NumThreads(SEGMENT_EVALUATE_THREADS_PER_GROUP, 1, 1)]
void WG_SegmentEvaluate(
    [MaxRecords(SEGMENT_EVALUATE_RECORDS_PER_GROUP)] GroupNodeInputRecords<SegmentEvalRecord> inRecs,
    uint GI : SV_GroupIndex,
    [MaxRecords(SEGMENT_EVALUATE_RECORDS_PER_GROUP * MAX_RECORDS_PER_SEGMENT)] NodeOutput<MeshletBucketRecord> ClusterCull1,
    [MaxRecordsSharedWith(ClusterCull1)] NodeOutput<MeshletBucketRecord> ClusterCull2,
    [MaxRecordsSharedWith(ClusterCull1)] NodeOutput<MeshletBucketRecord> ClusterCull4,
    [MaxRecordsSharedWith(ClusterCull1)] NodeOutput<MeshletBucketRecord> ClusterCull8,
    [MaxRecordsSharedWith(ClusterCull1)] NodeOutput<MeshletBucketRecord> ClusterCull16,
    [MaxRecordsSharedWith(ClusterCull1)] NodeOutput<MeshletBucketRecord> ClusterCull32,
    [MaxRecordsSharedWith(ClusterCull1)] NodeOutput<MeshletBucketRecord> ClusterCull64)
{
    const uint slot = GI;
    const uint inputCount = inRecs.Count();
    const bool slotActive = slot < inputCount;

    WGTelemetryAdd(WG_COUNTER_SEGMENT_EVALUATE_THREADS, 1);
    if (slot == 0) {
        WGTelemetryAdd(WG_COUNTER_SEGMENT_EVALUATE_COALESCED_LAUNCHES, 1);
        WGTelemetryAdd(WG_COUNTER_SEGMENT_EVALUATE_COALESCED_INPUT_RECORDS, inputCount);
    }

    bool emitBucket = false;
    MeshletBucketRecord bucketRecord = (MeshletBucketRecord)0;
    uint segMeshletCount = 0;

    // Decomposition counts for each bucket variant
    uint n64 = 0, n32 = 0, n16 = 0, n8 = 0, n4 = 0, n2 = 0, n1 = 0;

    if (slotActive) {
        const SegmentEvalRecord rec = inRecs[slot];
        WGTelemetryAdd(WG_COUNTER_SEGMENT_EVALUATE_SEGMENT_RECORDS, 1);

        StructuredBuffer<MeshInstanceClodOffsets> clodOffsets =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
        StructuredBuffer<CLodMeshMetadata> clodMeshMetadataBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::MeshMetadata)];
        const MeshInstanceClodOffsets off = clodOffsets[rec.instanceIndex];
        const CLodMeshMetadata clodMeshMetadata = clodMeshMetadataBuffer[off.clodMeshMetadataIndex];
        StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
        const PerMeshInstanceBuffer instanceData = perMeshInstanceBuffer[rec.instanceIndex];
        const uint objectBufferIndex = instanceData.perObjectBufferIndex;
        StructuredBuffer<PerObjectBuffer> perObjectBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
        const row_major matrix objectModelMatrix = perObjectBuffer[objectBufferIndex].model;
        StructuredBuffer<CullingCameraInfo> cameraInfos =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
        const CullingCameraInfo cam = cameraInfos[rec.viewId];
        StructuredBuffer<ClusterLODGroup> groups =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
        StructuredBuffer<ClusterLODGroupSegment> segments =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Segments)];

        const uint groupGlobalIndex = clodMeshMetadata.groupsBase + rec.ownerGroupId;
        const ClusterLODGroup grp = groups[groupGlobalIndex];
        const uint segGlobalIndex = clodMeshMetadata.segmentsBase + rec.segmentIndex;
        const ClusterLODGroupSegment seg = segments[segGlobalIndex];
        const float groupUniformScale = MaxAxisScale_RowVector(objectModelMatrix);

        // Used-groups tracking: mark the owning group as touched for streaming protection
        {
            RWStructuredBuffer<uint> usedGroupsCounter =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingTouchedGroupsCounter)];
            RWStructuredBuffer<uint> usedGroupsBuffer =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingTouchedGroups)];
            uint usedSlot = 0;
            InterlockedAdd(usedGroupsCounter[0], 1u, usedSlot);
            if (usedSlot < CLOD_USED_GROUPS_CAPACITY) {
                usedGroupsBuffer[usedSlot] = groupGlobalIndex;
            }
        }

        // LOD cut: condition 1 (own group needs refinement).
        // Condition 1: own group's error over threshold.
        const float3 ownGroupWorldCenter = mul(float4(grp.bounds.centerAndRadius.xyz, 1.0f), objectModelMatrix).xyz;
        const float ownGroupWorldRadius = grp.bounds.centerAndRadius.w * groupUniformScale;
        const float ownGroupErrorOverDistance = ErrorOverDistance(
            ownGroupWorldCenter,
            ownGroupWorldRadius,
            grp.bounds.error,
            groupUniformScale,
            cam.positionWorldSpace.xyz,
            cam.zNear);
        const bool ownGroupNeedsRefinement = (ownGroupErrorOverDistance >= cam.errorOverDistanceThreshold);

        // Condition 2 deferred to per-meshlet in ClusterCullBuckets.
        bool shouldEmit = ownGroupNeedsRefinement && (seg.meshletCount != 0);

        // Suppress emission when own group's pages are non-resident.
        {
            StructuredBuffer<CLodStreamingRuntimeState> runtimeState =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingRuntimeState)];
            const uint activeGroupScanCount = runtimeState[0].activeGroupScanCount;
            ByteAddressBuffer nonResidentBits =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingNonResidentBits)];
            if (shouldEmit && groupGlobalIndex < activeGroupScanCount) {
                if (CLodReadBit(nonResidentBits, groupGlobalIndex)) {
                    shouldEmit = false;
                }
            }
        }

        emitBucket = shouldEmit;

        if (emitBucket) {
            WGTelemetryAdd(WG_COUNTER_SEGMENT_EVALUATE_EMIT_BUCKET_THREADS, 1);

            const GroupPageMapEntry pageEntry = LoadGroupPageMapEntry(clodMeshMetadata.pageMapBase + grp.pageMapBase, seg.pageIndex);

            bucketRecord.instanceIndex = rec.instanceIndex;
            bucketRecord.viewId = rec.viewId;
            bucketRecord.groupId = rec.ownerGroupId;
            bucketRecord.childFirstLocalMeshletIndex = seg.firstMeshletInPage;
            bucketRecord.childLocalMeshletCount = 0; // set per-record below
            bucketRecord.sourceTag = rec.sourceTag;
            bucketRecord.pageSlabDescriptorIndex = pageEntry.slabDescriptorIndex;
            bucketRecord.pageSlabByteOffset = pageEntry.slabByteOffset;

            // Decompose meshlet count into bucket-sized records (max 8 records)
            segMeshletCount = seg.meshletCount;
            uint tail = segMeshletCount;
            uint budget = MAX_RECORDS_PER_SEGMENT;

            // fill with 64-buckets
            n64 = min(tail / 64, budget);
            tail -= n64 * 64;
            budget -= n64;

            // Greedily extract from tail when budget allows multi-record tail
            if (tail >= 32 && budget >= 2) { n32 = 1; tail -= 32; budget--; }
            if (tail >= 16 && budget >= 2) { n16 = 1; tail -= 16; budget--; }
            if (tail >= 8  && budget >= 2) { n8  = 1; tail -= 8;  budget--; }
            if (tail >= 4  && budget >= 2) { n4  = 1; tail -= 4;  budget--; }
            if (tail >= 2  && budget >= 2) { n2  = 1; tail -= 2;  budget--; }

            // Final tail bucket (round up to smallest fitting size)
            if      (tail > 32) { n64++; }
            else if (tail > 16) { n32++; }
            else if (tail > 8)  { n16++; }
            else if (tail > 4)  { n8++;  }
            else if (tail > 2)  { n4++;  }
            else if (tail > 1)  { n2++;  }
            else if (tail > 0)  { n1 = 1; }
        }
    }

    // Allocate output records for each variant
    ThreadNodeOutputRecords<MeshletBucketRecord> out64 = ClusterCull64.GetThreadNodeOutputRecords(n64);
    ThreadNodeOutputRecords<MeshletBucketRecord> out32 = ClusterCull32.GetThreadNodeOutputRecords(n32);
    ThreadNodeOutputRecords<MeshletBucketRecord> out16 = ClusterCull16.GetThreadNodeOutputRecords(n16);
    ThreadNodeOutputRecords<MeshletBucketRecord> out8  = ClusterCull8.GetThreadNodeOutputRecords(n8);
    ThreadNodeOutputRecords<MeshletBucketRecord> out4  = ClusterCull4.GetThreadNodeOutputRecords(n4);
    ThreadNodeOutputRecords<MeshletBucketRecord> out2  = ClusterCull2.GetThreadNodeOutputRecords(n2);
    ThreadNodeOutputRecords<MeshletBucketRecord> out1  = ClusterCull1.GetThreadNodeOutputRecords(n1);

    if (emitBucket) {
        uint offset = bucketRecord.childFirstLocalMeshletIndex;

        for (uint i = 0; i < n64; i++) {
            MeshletBucketRecord r = bucketRecord;
            r.childFirstLocalMeshletIndex = offset;
            r.childLocalMeshletCount = 64;
            out64[i] = r;
            offset += 64;
        }
        if (n32) {
            MeshletBucketRecord r = bucketRecord;
            r.childFirstLocalMeshletIndex = offset;
            r.childLocalMeshletCount = 32;
            out32.Get() = r;
            offset += 32;
        }
        if (n16) {
            MeshletBucketRecord r = bucketRecord;
            r.childFirstLocalMeshletIndex = offset;
            r.childLocalMeshletCount = 16;
            out16.Get() = r;
            offset += 16;
        }
        if (n8) {
            MeshletBucketRecord r = bucketRecord;
            r.childFirstLocalMeshletIndex = offset;
            r.childLocalMeshletCount = 8;
            out8.Get() = r;
            offset += 8;
        }
        if (n4) {
            MeshletBucketRecord r = bucketRecord;
            r.childFirstLocalMeshletIndex = offset;
            r.childLocalMeshletCount = 4;
            out4.Get() = r;
            offset += 4;
        }
        if (n2) {
            MeshletBucketRecord r = bucketRecord;
            r.childFirstLocalMeshletIndex = offset;
            r.childLocalMeshletCount = 2;
            out2.Get() = r;
            offset += 2;
        }
        if (n1) {
            MeshletBucketRecord r = bucketRecord;
            r.childFirstLocalMeshletIndex = offset;
            r.childLocalMeshletCount = 1;
            out1.Get() = r;
        }
    }

    out64.OutputComplete();
    out32.OutputComplete();
    out16.OutputComplete();
    out8.OutputComplete();
    out4.OutputComplete();
    out2.OutputComplete();
    out1.OutputComplete();
}


// Shared cluster-cull implementation called by each bucket-size variant.
// FIXED_LOOP_COUNT is the bucket size (1, 2, 4, 8, 16, 32, or 64) - all active lanes
// in a variant wave process the same number of iterations, eliminating WaveActiveMax divergence.
void ClusterCullBody(MeshletBucketRecord b, bool hasBucket, uint GI, uint inputCount, uint FIXED_LOOP_COUNT)
{
    // Telemetry (coalesced launch level)
    WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_THREADS, 1);
    if (hasBucket) {
        WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_IN_RANGE_THREADS, b.childLocalMeshletCount);
        if (b.sourceTag == CLOD_RECORD_SOURCE_REPLAY) {
            WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_CLUSTER_BUCKET_RECORDS_CONSUMED, 1);
        }
    }

    const uint4 allLaneMask = WaveActiveBallot(true);
    const uint allLeaderLane = WaveFirstLaneFromMask(allLaneMask);
    const bool isWaveLeader = (WaveGetLaneIndex() == allLeaderLane);
    if (isWaveLeader) {
        WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_WAVES, 1);
        WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_ACTIVE_LANES, inputCount);
    }

    // Pre-load per-bucket data (loaded once, reused across meshlets).
    // Only the camera fields needed for the hot frustum-culling loop are loaded here
    // (view matrix + 6 clip planes).  Occlusion-specific camera matrices (projection,
    // prevView, prevUnjitteredProjection) and prevModelMatrix are deferred to the
    // occlusion branch to reduce register pressure.
    bool pageValid = false;
    bool replaySource = false;
    row_major matrix objectModelMatrix = (float4x4)0;
    row_major matrix viewMatrix = (float4x4)0;
    float4 frustumPlanes[6];
    uint pageSlabDesc = 0;
    uint pageSlabOff = 0;
    uint pageMeshletCount = 0;
    uint pageDescriptorOffset = 0;
    uint depthMapDescriptorIndex = 0;
    uint2 depthRes = uint2(0, 0);
    uint numDepthMips = 0;
    float2 hzbUVScale = float2(0, 0);
    float groupUniformScale = 0.0f;
    CullingCameraInfo cam = (CullingCameraInfo)0;
    uint groupsBase = 0;
    uint meshBufferIndex = 0;
    uint activeGroupScanCount = 0;
    float ownGroupErrorOverDistance = 0.0f;
    uint objectBufferIndex = 0;

    if (hasBucket && b.pageSlabDescriptorIndex != 0) {
        pageValid = true;
        replaySource = (b.sourceTag == CLOD_RECORD_SOURCE_REPLAY);
        pageSlabDesc = b.pageSlabDescriptorIndex;
        pageSlabOff = b.pageSlabByteOffset;

        StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
        objectBufferIndex = perMeshInstanceBuffer[b.instanceIndex].perObjectBufferIndex;
        StructuredBuffer<PerObjectBuffer> perObjectBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
        objectModelMatrix = perObjectBuffer[objectBufferIndex].model;

        // Load only the camera fields needed for the hot culling loop.
        // Occlusion matrices are deferred to the occlusion branch.
        StructuredBuffer<Camera> cameras =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
        viewMatrix = cameras[b.viewId].view;
        [unroll] for (uint p = 0; p < 6; p++)
            frustumPlanes[p] = cameras[b.viewId].clippingPlanes[p].plane;

        // Load meshletCount (uint[0]) and descriptorOffset (uint[2]) from new 64-byte header
        ByteAddressBuffer slab = ResourceDescriptorHeap[pageSlabDesc];
        pageMeshletCount = slab.Load(pageSlabOff);         // meshletCount at offset 0
        pageDescriptorOffset = slab.Load(pageSlabOff + 8); // descriptorOffset at offset 8

        if (!cameras[b.viewId].isOrtho) {
            StructuredBuffer<CLodViewDepthSRVIndex> viewDepthSRVIndices =
                ResourceDescriptorHeap[CLOD_VIEW_DEPTH_SRV_INDICES_DESCRIPTOR_INDEX];
            depthMapDescriptorIndex = viewDepthSRVIndices[b.viewId].linearDepthSRVIndex;
            depthRes = uint2(cameras[b.viewId].depthResX, cameras[b.viewId].depthResY);
            numDepthMips = cameras[b.viewId].numDepthMips;
            hzbUVScale = cameras[b.viewId].UVScaleToNextPowerOf2;
        }

        // Per-meshlet condition 2 + streaming fallback state
        groupUniformScale = MaxAxisScale_RowVector(objectModelMatrix);
        meshBufferIndex = perMeshInstanceBuffer[b.instanceIndex].perMeshBufferIndex;

        StructuredBuffer<CullingCameraInfo> cameraInfos =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
        cam = cameraInfos[b.viewId];

        StructuredBuffer<MeshInstanceClodOffsets> clodOffsets =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
        StructuredBuffer<CLodMeshMetadata> clodMeshMetadataBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::MeshMetadata)];
        const MeshInstanceClodOffsets clodOff = clodOffsets[b.instanceIndex];
        groupsBase = clodMeshMetadataBuffer[clodOff.clodMeshMetadataIndex].groupsBase;

        // Own group EOD for streaming request priority
        {
            StructuredBuffer<ClusterLODGroup> groups =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
            const ClusterLODGroup ownGrp = groups[groupsBase + b.groupId];
            const float3 ownWorldCenter = mul(float4(ownGrp.bounds.centerAndRadius.xyz, 1.0f), objectModelMatrix).xyz;
            const float ownWorldRadius = ownGrp.bounds.centerAndRadius.w * groupUniformScale;
            ownGroupErrorOverDistance = ErrorOverDistance(
                ownWorldCenter, ownWorldRadius, ownGrp.bounds.error, groupUniformScale,
                cam.positionWorldSpace.xyz, cam.zNear);
        }

        StructuredBuffer<CLodStreamingRuntimeState> runtimeState =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingRuntimeState)];
        activeGroupScanCount = runtimeState[0].activeGroupScanCount;
    }

    // Meshlet loop — fixed iteration count eliminates WaveActiveMax divergence.
    // Lanes with fewer meshlets (e.g. replay count=1) simply skip inactive iterations.
    const uint meshletCount = hasBucket ? b.childLocalMeshletCount : 0;

    RWStructuredBuffer<VisibleCluster> visibleClusters =
        ResourceDescriptorHeap[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> visibleClusterCounter =
        ResourceDescriptorHeap[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];
    const uint visibleClusterCapacity = CLOD_VISIBLE_CLUSTERS_CAPACITY;

    uint totalSurvivors = 0;

    for (uint m = 0; m < FIXED_LOOP_COUNT; m++) {
        const bool active = (m < meshletCount) && pageValid;
        uint localMeshletIndex = 0;
        bool survives = false;

        if (active) {
            const uint localMeshlet = b.childFirstLocalMeshletIndex + m;

            if (localMeshlet < pageMeshletCount) {
                localMeshletIndex = localMeshlet;

                // Load per-meshlet descriptor (3 x Load4 = 48 bytes)
                CLodMeshletDescriptor desc = LoadMeshletDescriptor(pageSlabDesc, pageSlabOff, pageDescriptorOffset, localMeshlet);
                const float4 boundsSphere = desc.bounds;
                const BoundingSphere meshletBounds = { boundsSphere };
                const float3 meshletCenterViewSpace = ToViewSpace(meshletBounds.sphere.xyz, objectModelMatrix, viewMatrix);
                const float meshletRadiusWorld = meshletBounds.sphere.w * groupUniformScale;

                survives = replaySource || !SphereOutsideFrustumViewSpace(meshletCenterViewSpace, meshletRadiusWorld, frustumPlanes);

                if (!survives) {
                    WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_REJECTED_FRUSTUM, 1);
                }

                // Per-meshlet LOD condition 2: read refined group ID from descriptor.
                // Terminal meshlets (refinedGroupId < 0) pass automatically.
                // Non-terminal meshlets check if the child group is acceptable.
                if (survives) {
                    const int refinedGroupId = CLodDescRefinedGroupId(desc);
                    if (refinedGroupId >= 0) {
                        StructuredBuffer<ClusterLODGroup> groups =
                            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
                        const uint childGroupGlobalIndex = groupsBase + (uint)refinedGroupId;
                        const ClusterLODGroup childGrp = groups[childGroupGlobalIndex];
                        const float3 childWorldCenter = mul(float4(childGrp.bounds.centerAndRadius.xyz, 1.0f), objectModelMatrix).xyz;
                        const float childWorldRadius = childGrp.bounds.centerAndRadius.w * groupUniformScale;
                        const float childEOD = ErrorOverDistance(
                            childWorldCenter, childWorldRadius,
                            childGrp.bounds.error, groupUniformScale,
                            cam.positionWorldSpace.xyz, cam.zNear);

                        if (childEOD >= cam.errorOverDistanceThreshold) {
                            // Child exceeds the threshold; check residency.
                            ByteAddressBuffer nonResidentBits =
                                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingNonResidentBits)];
                            bool childResident = true;
                            if (childGroupGlobalIndex < activeGroupScanCount) {
                                childResident = !CLodReadBit(nonResidentBits, childGroupGlobalIndex);
                            }

                            if (childResident) {
                                survives = false;
                                WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_REJECTED_CONDITION2, 1);
                            } else {
                                WGTelemetryAdd(WG_COUNTER_SEGMENT_EVALUATE_NON_RESIDENT_REFINED_CHILD_THREADS, 1);
                                if (childGroupGlobalIndex < activeGroupScanCount) {
                                    RWStructuredBuffer<CLodStreamingRequest> loadRequests =
                                        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingLoadRequests)];
                                    RWStructuredBuffer<uint> loadRequestCounter =
                                        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingLoadCounter)];
                                    uint requestIndex = 0;
                                    InterlockedAdd(loadRequestCounter[0], 1u, requestIndex);
                                    if (requestIndex < CLOD_STREAM_REQUEST_CAPACITY) {
                                        CLodStreamingRequest req = (CLodStreamingRequest)0;
                                        req.groupGlobalIndex = childGroupGlobalIndex;
                                        req.meshInstanceIndex = b.instanceIndex;
                                        req.meshBufferIndex = meshBufferIndex;
                                        req.viewId = CLodPackViewPriority(b.viewId, ownGroupErrorOverDistance);
                                        loadRequests[requestIndex] = req;
                                    }
                                }
                            }
                        }
                    }
                }

                if (survives && CLodWorkGraphOcclusionEnabled() && depthMapDescriptorIndex != 0) {
                    bool occlusionCulled = false;
                    // Lazy-load only the occlusion-specific camera matrices when needed,
                    // keeping them off the register file during the main frustum/LOD loop.
                    StructuredBuffer<Camera> occCameras =
                        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
                    if (replaySource) {
                        // Phase 2 replay: HZB is from this frame's Phase 1 depth,
                        // so test current-frame bounding spheres.
                        OcclusionCullingPerspectiveTexture2D(
                            occlusionCulled,
                            depthRes, numDepthMips, hzbUVScale,
                            occCameras[b.viewId].projection,
                            meshletCenterViewSpace,
                            -meshletCenterViewSpace.z,
                            meshletRadiusWorld,
                            depthMapDescriptorIndex);
                    } else {
                        // Phase 1: HZB is from previous frame's depth,
                        // so reproject bounding sphere into previous frame's camera space.
                        StructuredBuffer<PerObjectBuffer> prevObjBuf =
                            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
                        const row_major matrix prevModelMatrix = prevObjBuf[objectBufferIndex].prevModel;
                        const float prevMeshletScale = MaxAxisScale_RowVector(prevModelMatrix);
                        const float3 prevMeshletCenterViewSpace = ToViewSpace(meshletBounds.sphere.xyz, prevModelMatrix, occCameras[b.viewId].prevView);
                        const float prevMeshletRadiusWorld = meshletBounds.sphere.w * prevMeshletScale;
                        OcclusionCullingPerspectiveTexture2D(
                            occlusionCulled,
                            depthRes, numDepthMips, hzbUVScale,
                            occCameras[b.viewId].prevUnjitteredProjection,
                            prevMeshletCenterViewSpace,
                            -prevMeshletCenterViewSpace.z,
                            prevMeshletRadiusWorld,
                            depthMapDescriptorIndex);
                    }
                    if (occlusionCulled) {
                        if (!replaySource) {
                            ReplayTryAppendMeshlet(
                                b.instanceIndex,
                                b.viewId,
                                b.groupId,
                                localMeshlet,
                                pageSlabDesc,
                                pageSlabOff);
                        }
                        survives = false;
                        WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_REJECTED_OCCLUSION, 1);
                    }
                }
            } else {
                WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_REJECTED_PAGE_BOUNDS, 1);
            }
        } else {
            WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_REJECTED_OUT_OF_RANGE, 1);
        }

        WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_MESHLET_ITERATIONS, active ? 1 : 0);

        // Wave-cooperative visible cluster output.
        const bool contributes = active && survives;
        const uint4 survivingMask = WaveActiveBallot(contributes);
        const uint survivingCount = CountBits128(survivingMask);
        totalSurvivors += survivingCount;

        if (survivingCount > 0) {
            const uint leaderLane = WaveFirstLaneFromMask(survivingMask);
            const uint laneRank = GetLaneRankInGroup(survivingMask, WaveGetLaneIndex());

            uint baseIndex = 0;
            if (WaveGetLaneIndex() == leaderLane) {
                InterlockedAdd(visibleClusterCounter[0], survivingCount, baseIndex);
            }
            baseIndex = WaveReadLaneAt(baseIndex, leaderLane);

            const uint availableCount =
                (baseIndex < visibleClusterCapacity)
                    ? min(survivingCount, visibleClusterCapacity - baseIndex)
                    : 0u;

            if (WaveGetLaneIndex() == leaderLane && (baseIndex + survivingCount > visibleClusterCapacity)) {
                InterlockedMin(visibleClusterCounter[0], visibleClusterCapacity);
            }

            if (isWaveLeader) {
                WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_VISIBLE_CLUSTER_WRITES, availableCount);
            }

            if (contributes && (laneRank < availableCount)) {
                const uint index = baseIndex + laneRank;

                VisibleCluster vc = (VisibleCluster)0;
                vc.instanceID = b.instanceIndex;
                vc.localMeshletIndex = localMeshletIndex;
                vc.viewID = b.viewId;
                vc.groupID = b.groupId;
                vc.pageSlabDescriptorIndex = b.pageSlabDescriptorIndex;
                vc.pageSlabByteOffset = b.pageSlabByteOffset;
                visibleClusters[index] = vc;
            }
        }
    }

    if (isWaveLeader) {
        WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_SURVIVING_LANES, totalSurvivors);
        if (totalSurvivors == 0) {
            WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_ZERO_SURVIVOR_WAVES, 1);
        }
    }
}

// ClusterCull variant entry points — one per bucket size.
// Each variant processes a fixed number of meshlets per lane, eliminating wave divergence.

[Shader("node")]
[NodeID("ClusterCull1")]
[NodeLaunch("coalescing")]
[NumThreads(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP, 1, 1)]
void WG_ClusterCull1(
    [MaxRecords(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP)] GroupNodeInputRecords<MeshletBucketRecord> inRecs,
    uint GI : SV_GroupIndex)
{
    const uint inputCount = inRecs.Count();
    const bool hasBucket = GI < inputCount;
    MeshletBucketRecord b = (MeshletBucketRecord)0;
    if (hasBucket) b = inRecs[GI];
    ClusterCullBody(b, hasBucket, GI, inputCount, 1);
}

[Shader("node")]
[NodeID("ClusterCull2")]
[NodeLaunch("coalescing")]
[NumThreads(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP, 1, 1)]
void WG_ClusterCull2(
    [MaxRecords(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP)] GroupNodeInputRecords<MeshletBucketRecord> inRecs,
    uint GI : SV_GroupIndex)
{
    const uint inputCount = inRecs.Count();
    const bool hasBucket = GI < inputCount;
    MeshletBucketRecord b = (MeshletBucketRecord)0;
    if (hasBucket) b = inRecs[GI];
    ClusterCullBody(b, hasBucket, GI, inputCount, 2);
}

[Shader("node")]
[NodeID("ClusterCull4")]
[NodeLaunch("coalescing")]
[NumThreads(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP, 1, 1)]
void WG_ClusterCull4(
    [MaxRecords(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP)] GroupNodeInputRecords<MeshletBucketRecord> inRecs,
    uint GI : SV_GroupIndex)
{
    const uint inputCount = inRecs.Count();
    const bool hasBucket = GI < inputCount;
    MeshletBucketRecord b = (MeshletBucketRecord)0;
    if (hasBucket) b = inRecs[GI];
    ClusterCullBody(b, hasBucket, GI, inputCount, 4);
}

[Shader("node")]
[NodeID("ClusterCull8")]
[NodeLaunch("coalescing")]
[NumThreads(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP, 1, 1)]
void WG_ClusterCull8(
    [MaxRecords(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP)] GroupNodeInputRecords<MeshletBucketRecord> inRecs,
    uint GI : SV_GroupIndex)
{
    const uint inputCount = inRecs.Count();
    const bool hasBucket = GI < inputCount;
    MeshletBucketRecord b = (MeshletBucketRecord)0;
    if (hasBucket) b = inRecs[GI];
    ClusterCullBody(b, hasBucket, GI, inputCount, 8);
}

[Shader("node")]
[NodeID("ClusterCull16")]
[NodeLaunch("coalescing")]
[NumThreads(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP, 1, 1)]
void WG_ClusterCull16(
    [MaxRecords(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP)] GroupNodeInputRecords<MeshletBucketRecord> inRecs,
    uint GI : SV_GroupIndex)
{
    const uint inputCount = inRecs.Count();
    const bool hasBucket = GI < inputCount;
    MeshletBucketRecord b = (MeshletBucketRecord)0;
    if (hasBucket) b = inRecs[GI];
    ClusterCullBody(b, hasBucket, GI, inputCount, 16);
}

[Shader("node")]
[NodeID("ClusterCull32")]
[NodeLaunch("coalescing")]
[NumThreads(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP, 1, 1)]
void WG_ClusterCull32(
    [MaxRecords(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP)] GroupNodeInputRecords<MeshletBucketRecord> inRecs,
    uint GI : SV_GroupIndex)
{
    const uint inputCount = inRecs.Count();
    const bool hasBucket = GI < inputCount;
    MeshletBucketRecord b = (MeshletBucketRecord)0;
    if (hasBucket) b = inRecs[GI];
    ClusterCullBody(b, hasBucket, GI, inputCount, 32);
}

[Shader("node")]
[NodeID("ClusterCull64")]
[NodeLaunch("coalescing")]
[NumThreads(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP, 1, 1)]
void WG_ClusterCull64(
    [MaxRecords(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP)] GroupNodeInputRecords<MeshletBucketRecord> inRecs,
    uint GI : SV_GroupIndex)
{
    const uint inputCount = inRecs.Count();
    const bool hasBucket = GI < inputCount;
    MeshletBucketRecord b = (MeshletBucketRecord)0;
    if (hasBucket) b = inRecs[GI];
    ClusterCullBody(b, hasBucket, GI, inputCount, 64);
}