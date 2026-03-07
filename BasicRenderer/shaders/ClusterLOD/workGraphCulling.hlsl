// Compile with DXC target: lib_6_8 (Shader Model 6.8)
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/indirectCommands.hlsli"
#include "include/waveIntrinsicsHelpers.hlsli"
#include "include/occlusionCulling.hlsli"
#include "PerPassRootConstants/clodRootConstants.h"
#include "Include/clodStructs.hlsli"

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
    uint isGroup; // 1 => leaf references group, 0 => internal node references children
    uint indexOrOffset; // if isGroup: groupId (relative to groupsBase)
                         // else: childOffset (relative to lodNodesBase)
    uint countMinusOne; // if isGroup: groupClusterCountMinusOne (optional; can ignore)
                         // else: childCountMinusOne
    uint pad0;
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
static const uint WG_COUNTER_TRAVERSE_GROUP_RECORDS = 11;

static const uint WG_COUNTER_GROUP_EVALUATE_THREADS = 12;
static const uint WG_COUNTER_GROUP_EVALUATE_GROUP_RECORDS = 13;
static const uint WG_COUNTER_GROUP_EVALUATE_CULLED_GROUP_RECORDS = 14;
static const uint WG_COUNTER_GROUP_EVALUATE_REJECTED_BY_ERROR_RECORDS = 15;
static const uint WG_COUNTER_GROUP_EVALUATE_ACTIVE_CHILD_THREADS = 16;
static const uint WG_COUNTER_GROUP_EVALUATE_EMIT_BUCKET_THREADS = 17;
static const uint WG_COUNTER_GROUP_EVALUATE_REFINED_TRAVERSAL_THREADS = 18;
static const uint WG_COUNTER_GROUP_EVALUATE_NON_RESIDENT_REFINED_CHILD_THREADS = 19;
static const uint WG_COUNTER_GROUP_EVALUATE_NON_RESIDENT_FALLBACK_BUCKET_THREADS = 20;

static const uint WG_COUNTER_CLUSTER_CULL_THREADS = 21;
static const uint WG_COUNTER_CLUSTER_CULL_IN_RANGE_THREADS = 22;
static const uint WG_COUNTER_CLUSTER_CULL_WAVES = 23;
static const uint WG_COUNTER_CLUSTER_CULL_ACTIVE_LANES = 24;
static const uint WG_COUNTER_CLUSTER_CULL_SURVIVING_LANES = 25;
static const uint WG_COUNTER_CLUSTER_CULL_ZERO_SURVIVOR_WAVES = 26;
static const uint WG_COUNTER_CLUSTER_CULL_VISIBLE_CLUSTER_WRITES = 27;

static const uint WG_COUNTER_TRAVERSE_COALESCED_LAUNCHES = 28;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_RECORDS = 29;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_1 = 30;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_2 = 31;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_3 = 32;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_4 = 33;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_5 = 34;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_6 = 35;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_7 = 36;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_8 = 37;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_LAUNCHES = 38;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_RECORDS = 39;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_1 = 40;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_2 = 41;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_3 = 42;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_4 = 43;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_5 = 44;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_6 = 45;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_7 = 46;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_8 = 47;

static const uint WG_COUNTER_PHASE1_OCCLUSION_NODE_REPLAY_ENQUEUE_ATTEMPTS = 48;
static const uint WG_COUNTER_PHASE1_OCCLUSION_GROUP_REPLAY_ENQUEUE_ATTEMPTS = 49;
static const uint WG_COUNTER_PHASE1_OCCLUSION_CLUSTER_REPLAY_ENQUEUE_ATTEMPTS = 50;

static const uint WG_COUNTER_PHASE2_REPLAY_NODE_GROUP_LAUNCHES = 51;
static const uint WG_COUNTER_PHASE2_REPLAY_NODE_GROUP_INPUT_RECORDS = 52;
static const uint WG_COUNTER_PHASE2_REPLAY_NODE_INPUT_RECORDS = 53;
static const uint WG_COUNTER_PHASE2_REPLAY_GROUP_INPUT_RECORDS = 54;
static const uint WG_COUNTER_PHASE2_REPLAY_NODE_RECORDS_EMITTED = 55;
static const uint WG_COUNTER_PHASE2_REPLAY_GROUP_RECORDS_EMITTED = 56;

static const uint WG_COUNTER_PHASE2_REPLAY_MESHLET_LAUNCHES = 57;
static const uint WG_COUNTER_PHASE2_REPLAY_MESHLET_INPUT_RECORDS = 58;
static const uint WG_COUNTER_PHASE2_REPLAY_MESHLET_BUCKET_RECORDS_EMITTED = 59;

static const uint WG_COUNTER_PHASE2_REPLAY_TRAVERSE_RECORDS_CONSUMED = 60;
static const uint WG_COUNTER_PHASE2_REPLAY_GROUP_RECORDS_CONSUMED = 61;
static const uint WG_COUNTER_PHASE2_REPLAY_CLUSTER_BUCKET_RECORDS_CONSUMED = 62;

static const uint CLOD_STREAM_REQUEST_CAPACITY = (1u << 16);
static const uint CLOD_STREAM_VIEWID_MASK = 0xFFFFu;
static const uint CLOD_STREAM_PRIORITY_SHIFT = 16u;

static const uint CLOD_RECORD_SOURCE_PASS1 = 0;
static const uint CLOD_RECORD_SOURCE_REPLAY = 1;

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

static const uint COALESCING_THREADS_PER_GROUP = 1;
static const uint BVH_MAX_CHILDREN = 32;
static const uint COALESCED_INPUT_COUNT_HISTOGRAM_BUCKETS = 8;

static const uint TRAVERSE_RECORDS_PER_GROUP = COALESCING_THREADS_PER_GROUP;
static const uint GROUP_EVALUATE_RECORDS_PER_GROUP = COALESCING_THREADS_PER_GROUP;

void WGTelemetryAdd(uint counterIndex, uint value)
{
    if (CLOD_WORKGRAPH_TELEMETRY_ENABLED == 0)
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

bool ReplayTryAppendNodeGroup(uint type, uint instanceIndex, uint viewId, uint nodeOrGroupId)
{
    if (type == CLOD_REPLAY_RECORD_TYPE_NODE) {
        WGTelemetryAdd(WG_COUNTER_PHASE1_OCCLUSION_NODE_REPLAY_ENQUEUE_ATTEMPTS, 1);
    }
    else {
        WGTelemetryAdd(WG_COUNTER_PHASE1_OCCLUSION_GROUP_REPLAY_ENQUEUE_ATTEMPTS, 1);
    }

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
    replayBuffer.Store4(byteOffset, uint4(type, instanceIndex, viewId, nodeOrGroupId));
    replayBuffer.Store(byteOffset + 16u, 0u);
    return true;
}

bool ReplayTryAppendMeshlet(uint instanceIndex, uint viewId, uint groupId, uint localMeshletIndex)
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
    replayBuffer.Store(byteOffset + 16u, localMeshletIndex);
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

struct GroupEvalRecord
{
    uint instanceIndex;
    uint groupId;
    uint viewId;
    uint sourceTag;
    uint allowRefine;
};

struct MeshletBucketRecord
{
    uint instanceIndex;
    uint viewId;

    uint groupId;
    uint childFirstLocalMeshletIndex;
    uint childLocalMeshletCount;
    uint sourceTag;

    uint3 dispatchGrid : SV_DispatchGrid; // drives broadcasting node launch
};

// Phase-2 entry: replay queued node/group work items.
[Shader("node")]
[NodeID("ReplayNodeGroup")]
[NodeLaunch("coalescing")]
[NumThreads(32, 1, 1)]
[NodeIsProgramEntry]
void WG_ReplayNodeGroup(
    [MaxRecords(32)] GroupNodeInputRecords<CLodNodeGroupReplayRecord> inRecs,
    uint3 gtid : SV_GroupThreadID,
    [MaxRecords(32)] NodeOutput<TraverseNodeRecord> TraverseNodes,
    [MaxRecords(32)] NodeOutput<GroupEvalRecord> GroupEvaluate)
{
    const uint lane = gtid.x;
    const uint inputCount = inRecs.Count();
    const bool inRange = lane < inputCount;

    if (lane == 0) {
        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_NODE_GROUP_LAUNCHES, 1);
        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_NODE_GROUP_INPUT_RECORDS, inputCount);
    }

    bool emitTraverse = false;
    bool emitGroup = false;
    TraverseNodeRecord traverseRecord = (TraverseNodeRecord)0;
    GroupEvalRecord groupRecord = (GroupEvalRecord)0;

    if (inRange) {
        const CLodNodeGroupReplayRecord rec = inRecs[lane];
        if (rec.type == CLOD_REPLAY_RECORD_TYPE_NODE) {
            emitTraverse = true;
            traverseRecord.instanceIndex = rec.instanceIndex;
            traverseRecord.nodeId = rec.nodeOrGroupId;
            traverseRecord.viewId = rec.viewId;
            traverseRecord.sourceTag = CLOD_RECORD_SOURCE_REPLAY;
            traverseRecord.allowRefine = 1u;
            WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_NODE_INPUT_RECORDS, 1);
        }
        else if (rec.type == CLOD_REPLAY_RECORD_TYPE_GROUP) {
            emitGroup = true;
            groupRecord.instanceIndex = rec.instanceIndex;
            groupRecord.groupId = rec.nodeOrGroupId;
            groupRecord.viewId = rec.viewId;
            groupRecord.sourceTag = CLOD_RECORD_SOURCE_REPLAY;
            groupRecord.allowRefine = 1u;
            WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_GROUP_INPUT_RECORDS, 1);
        }
    }

    ThreadNodeOutputRecords<TraverseNodeRecord> outTraverse =
        TraverseNodes.GetThreadNodeOutputRecords(emitTraverse ? 1 : 0);
    ThreadNodeOutputRecords<GroupEvalRecord> outGroup =
        GroupEvaluate.GetThreadNodeOutputRecords(emitGroup ? 1 : 0);

    if (emitTraverse) {
        outTraverse.Get() = traverseRecord;
        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_NODE_RECORDS_EMITTED, 1);
    }

    if (emitGroup) {
        outGroup.Get() = groupRecord;
        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_GROUP_RECORDS_EMITTED, 1);
    }

    outTraverse.OutputComplete();
    outGroup.OutputComplete();
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
    [MaxRecords(32)] NodeOutput<MeshletBucketRecord> ClusterCullBuckets)
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
        ClusterCullBuckets.GetThreadNodeOutputRecords(emitBucket ? 1 : 0);

    if (emitBucket) {
        MeshletBucketRecord bucket = (MeshletBucketRecord)0;
        bucket.instanceIndex = meshletRecord.instanceIndex;
        bucket.viewId = meshletRecord.viewId;
        bucket.groupId = meshletRecord.groupId;
        bucket.childFirstLocalMeshletIndex = meshletRecord.localMeshletIndex;
        bucket.childLocalMeshletCount = 1;
        bucket.sourceTag = CLOD_RECORD_SOURCE_REPLAY;
        bucket.dispatchGrid = uint3(1, 1, 1);
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
[NumThreads(COALESCING_THREADS_PER_GROUP, 1, 1)]
[NodeMaxRecursionDepth(25)]
void WG_TraverseNodes(
    [MaxRecords(TRAVERSE_RECORDS_PER_GROUP)] GroupNodeInputRecords<TraverseNodeRecord> inRecs,
    uint GI : SV_GroupIndex,
    [MaxRecords(TRAVERSE_RECORDS_PER_GROUP * BVH_MAX_CHILDREN)] NodeOutput<TraverseNodeRecord> TraverseNodes,
    [MaxRecords(TRAVERSE_RECORDS_PER_GROUP)] NodeOutput<GroupEvalRecord> GroupEvaluate)
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
    bool emitGroup = false;
    TraverseNodeRecord childRecords[BVH_MAX_CHILDREN];
    GroupEvalRecord groupRecord = (GroupEvalRecord)0;

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
            const float3 nodeCenterWorldSpace = mul(float4(nodeCenterObjectSpace, 1.0f), objectModelMatrix).xyz;
            const float nodeErrorOverDistance = ErrorOverDistance(
                nodeCenterWorldSpace,
                nodeRadiusWorld,
                node.metric.maxQuadricError,
                nodeUniformScale,
                cam.positionWorldSpace.xyz,
                cam.zNear);

            const bool nodeWantsTraversal = parentAllowsRefine && (nodeErrorOverDistance >= cam.errorOverDistanceThreshold);
            if (!nodeWantsTraversal) {
                WGTelemetryAdd(WG_COUNTER_TRAVERSE_REJECTED_BY_ERROR_RECORDS, 1);
            }
            else if (node.range.isGroup == 0) {
                bool occlusionCulled = false;
                if (!camera.isOrtho) {
                    StructuredBuffer<CLodViewDepthSRVIndex> viewDepthSRVIndices =
                        ResourceDescriptorHeap[CLOD_VIEW_DEPTH_SRV_INDICES_DESCRIPTOR_INDEX];
                    const uint depthMapDescriptorIndex = viewDepthSRVIndices[rec.viewId].linearDepthSRVIndex;
                    if (depthMapDescriptorIndex != 0) {
                        OcclusionCullingPerspectiveTexture2D(
                            occlusionCulled,
                            camera,
                            nodeCenterViewSpace,
                            -nodeCenterViewSpace.z,
                            nodeRadiusWorld,
                            depthMapDescriptorIndex);
                    }
                }

                if (occlusionCulled) {
                    if (!replaySource) {
                        ReplayTryAppendNodeGroup(
                            CLOD_REPLAY_RECORD_TYPE_NODE,
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
            else {
                emitGroup = true;
                groupRecord.instanceIndex = rec.instanceIndex;
                groupRecord.viewId = rec.viewId;
                groupRecord.groupId = node.range.indexOrOffset;
                groupRecord.sourceTag = rec.sourceTag;
                groupRecord.allowRefine = nodeWantsTraversal ? 1u : 0u;
            }
        }
    }

    ThreadNodeOutputRecords<TraverseNodeRecord> outNodes =
        TraverseNodes.GetThreadNodeOutputRecords(emitTraverseCount);
    ThreadNodeOutputRecords<GroupEvalRecord> outGroups =
        GroupEvaluate.GetThreadNodeOutputRecords(emitGroup ? 1 : 0);

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

    if (emitGroup) {
        outGroups.Get() = groupRecord;
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_GROUP_RECORDS, 1);
    }

    outNodes.OutputComplete();
    outGroups.OutputComplete();
}
#define CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP 32
// Node: GroupEvaluate
[Shader("node")]
[NodeID("GroupEvaluate")]
[NodeLaunch("coalescing")]
[NumThreads(COALESCING_THREADS_PER_GROUP, 1, 1)]
void WG_GroupEvaluate(
    [MaxRecords(GROUP_EVALUATE_RECORDS_PER_GROUP)] GroupNodeInputRecords<GroupEvalRecord> inRecs,
    uint GI : SV_GroupIndex,
    [MaxRecords(GROUP_EVALUATE_RECORDS_PER_GROUP * BVH_MAX_CHILDREN)] NodeOutput<MeshletBucketRecord> ClusterCullBuckets)
{
    const uint slot = GI;
    const uint inputCount = inRecs.Count();
    const bool slotActive = slot < inputCount;

    WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_THREADS, 1);
    if (slot == 0) {
        WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_COALESCED_LAUNCHES, 1);
        WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_RECORDS, inputCount);
        if (inputCount > 0 && inputCount <= COALESCED_INPUT_COUNT_HISTOGRAM_BUCKETS) {
            WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_1 + (inputCount - 1), 1);
        }
    }

    uint emitBucketCount = 0;
    MeshletBucketRecord bucketRecords[BVH_MAX_CHILDREN];

    if (slotActive) {
        const GroupEvalRecord rec = inRecs[slot];
        if (rec.sourceTag == CLOD_RECORD_SOURCE_REPLAY) {
            WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_GROUP_RECORDS_CONSUMED, 1);
        }
        const bool replaySource = (rec.sourceTag == CLOD_RECORD_SOURCE_REPLAY);
        WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_GROUP_RECORDS, 1);

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
        StructuredBuffer<Camera> cameras =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
        const Camera camera = cameras[rec.viewId];
        StructuredBuffer<ClusterLODGroup> groups =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];

        const uint groupIndex = clodMeshMetadata.groupsBase + rec.groupId;
        const ClusterLODGroup grp = groups[groupIndex];
        const float groupUniformScale = MaxAxisScale_RowVector(objectModelMatrix);
        const float3 groupCenterObjectSpace = grp.bounds.centerAndRadius.xyz;
        const float3 groupCenterViewSpace = ToViewSpace(groupCenterObjectSpace, objectModelMatrix, camera.view);
        const float groupRadiusWorld = grp.bounds.centerAndRadius.w * groupUniformScale;
        const bool groupCulled = !replaySource && SphereOutsideFrustumViewSpace(groupCenterViewSpace, groupRadiusWorld, camera);

        if (groupCulled) {
            WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_CULLED_GROUP_RECORDS, 1);
        }
        else {
            const uint childBase = clodMeshMetadata.childrenBase + grp.firstChild;
            const uint clampedChildCount = min(grp.childCount, BVH_MAX_CHILDREN);
            const uint terminalChildCount = min(grp.terminalChildCount, BVH_MAX_CHILDREN);
            StructuredBuffer<CullingCameraInfo> cameraInfos =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
            const CullingCameraInfo cam = cameraInfos[rec.viewId];
            StructuredBuffer<ClusterLODChild> children =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Children)];
            StructuredBuffer<CLodStreamingRuntimeState> runtimeState =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingRuntimeState)];
            const uint activeGroupScanCount = runtimeState[0].activeGroupScanCount;
            ByteAddressBuffer nonResidentBits =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingNonResidentBits)];
            RWStructuredBuffer<CLodStreamingRequest> loadRequests =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingLoadRequests)];
            RWStructuredBuffer<uint> loadRequestCounter =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingLoadCounter)];
            StructuredBuffer<CLodViewDepthSRVIndex> viewDepthSRVIndices =
                ResourceDescriptorHeap[CLOD_VIEW_DEPTH_SRV_INDICES_DESCRIPTOR_INDEX];
            const uint depthMapDescriptorIndex = camera.isOrtho
                ? 0u
                : viewDepthSRVIndices[rec.viewId].linearDepthSRVIndex;
            const float3 generatingWorldSpaceCenter = mul(float4(grp.bounds.centerAndRadius.xyz, 1.0f), objectModelMatrix).xyz;
            const float generatingWorldRadius = grp.bounds.centerAndRadius.w * groupUniformScale;

            [unroll]
            for (uint childIndex = 0; childIndex < BVH_MAX_CHILDREN; ++childIndex) {
                if (childIndex >= clampedChildCount) {
                    break;
                }

                WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_ACTIVE_CHILD_THREADS, 1);

                const ClusterLODChild child = children[childBase + childIndex];
                const bool forceBucket = (rec.allowRefine == 0u) || (childIndex < terminalChildCount) || (child.refinedGroup < 0);

                bool generatingGroupWantsTraversal = false;
                bool replayedOccludedRefinedGroup = false;
                bool nonResidentRefinedChild = false;

                if (!forceBucket && child.refinedGroup >= 0) {
                    const uint refinedGroupGlobalIndex = clodMeshMetadata.groupsBase + (uint) child.refinedGroup;
                    const ClusterLODGroup refinedGrp = groups[refinedGroupGlobalIndex];
                    bool refinedResident = true;

                    if (refinedGroupGlobalIndex < activeGroupScanCount) {
                        refinedResident = !CLodReadBit(nonResidentBits, refinedGroupGlobalIndex);
                    }

                    if (!refinedResident) {
                        nonResidentRefinedChild = true;
                        WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_NON_RESIDENT_REFINED_CHILD_THREADS, 1);

                        const float fallbackErrorOverDistance = ErrorOverDistance(
                            generatingWorldSpaceCenter,
                            generatingWorldRadius,
                            grp.bounds.error,
                            groupUniformScale,
                            cam.positionWorldSpace.xyz,
                            cam.zNear);

                        if (refinedGroupGlobalIndex < activeGroupScanCount) {
                            uint requestIndex = 0;
                            InterlockedAdd(loadRequestCounter[0], 1u, requestIndex);
                            if (requestIndex < CLOD_STREAM_REQUEST_CAPACITY) {
                                CLodStreamingRequest req = (CLodStreamingRequest)0;
                                req.groupGlobalIndex = refinedGroupGlobalIndex;
                                req.meshInstanceIndex = rec.instanceIndex;
                                req.meshBufferIndex = instanceData.perMeshBufferIndex;
                                req.viewId = CLodPackViewPriority(rec.viewId, fallbackErrorOverDistance);
                                loadRequests[requestIndex] = req;
                            }
                        }
                    }
                    else {
                        const float3 refinedWorldSpaceCenter = mul(float4(refinedGrp.bounds.centerAndRadius.xyz, 1.0f), objectModelMatrix).xyz;
                        const float refinedWorldRadius = refinedGrp.bounds.centerAndRadius.w * groupUniformScale;
                        const float3 refinedViewSpaceCenter = mul(float4(refinedWorldSpaceCenter, 1.0f), camera.view).xyz;
                        const bool refinedCulled = !replaySource && SphereOutsideFrustumViewSpace(refinedViewSpaceCenter, refinedWorldRadius, camera);
                        const float refinedErrorOverDistance = ErrorOverDistance(
                            refinedWorldSpaceCenter,
                            refinedWorldRadius,
                            refinedGrp.bounds.error,
                            groupUniformScale,
                            cam.positionWorldSpace.xyz,
                            cam.zNear);

                        bool occlusionCulled = false;
                        if (!refinedCulled && depthMapDescriptorIndex != 0) {
                            OcclusionCullingPerspectiveTexture2D(
                                occlusionCulled,
                                camera,
                                refinedViewSpaceCenter,
                                -refinedViewSpaceCenter.z,
                                refinedWorldRadius,
                                depthMapDescriptorIndex);
                        }

                        if (occlusionCulled) {
                            if (!replaySource) {
                                ReplayTryAppendNodeGroup(
                                    CLOD_REPLAY_RECORD_TYPE_GROUP,
                                    rec.instanceIndex,
                                    rec.viewId,
                                    (uint)child.refinedGroup);
                            }
                            replayedOccludedRefinedGroup = true;
                        }
                        else {
                            generatingGroupWantsTraversal = !refinedCulled && (refinedErrorOverDistance >= cam.errorOverDistanceThreshold);
                        }
                    }

                    if (generatingGroupWantsTraversal) {
                        WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_REFINED_TRAVERSAL_THREADS, 1);
                    }
                }

                const bool shouldEmitCurrentLevelBucket =
                    forceBucket || nonResidentRefinedChild || !generatingGroupWantsTraversal;
                const bool emitBucket = (child.localMeshletCount != 0)
                    && shouldEmitCurrentLevelBucket
                    && !replayedOccludedRefinedGroup;

                if (emitBucket && nonResidentRefinedChild) {
                    WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_NON_RESIDENT_FALLBACK_BUCKET_THREADS, 1);
                }

                if (emitBucket) {
                    WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_EMIT_BUCKET_THREADS, 1);

                    MeshletBucketRecord bucketRecord = (MeshletBucketRecord)0;
                    bucketRecord.instanceIndex = rec.instanceIndex;
                    bucketRecord.viewId = rec.viewId;
                    bucketRecord.groupId = rec.groupId;
                    bucketRecord.childFirstLocalMeshletIndex = child.firstLocalMeshletIndex;
                    bucketRecord.childLocalMeshletCount = child.localMeshletCount;
                    bucketRecord.sourceTag = rec.sourceTag;
                    bucketRecord.dispatchGrid = uint3(
                        (bucketRecord.childLocalMeshletCount + CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP - 1) / CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP,
                        1,
                        1);

                    bucketRecords[emitBucketCount] = bucketRecord;
                    emitBucketCount++;
                }
            }
        }
    }

    ThreadNodeOutputRecords<MeshletBucketRecord> outBuckets =
        ClusterCullBuckets.GetThreadNodeOutputRecords(emitBucketCount);

    if (emitBucketCount > 0) {
        [unroll]
        for (uint bucketIndex = 0; bucketIndex < BVH_MAX_CHILDREN; ++bucketIndex) {
            if (bucketIndex >= emitBucketCount) {
                break;
            }

            outBuckets[bucketIndex] = bucketRecords[bucketIndex];
        }
    }

    outBuckets.OutputComplete();
}



// Node: ClusterCull (bin to rasterize vs 2nd pass)
[Shader("node")]
[NodeID("ClusterCullBuckets")]
[NodeLaunch("broadcasting")]
[NumThreads(CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP, 1, 1)]
[NodeMaxDispatchGrid(64, 1, 1)] // TODO: Sane max?
void WG_ClusterCullBuckets(
    DispatchNodeInputRecord< MeshletBucketRecord> inRec,
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID){

    const MeshletBucketRecord b = inRec.Get();
    const uint i = DTid.x;

    const bool inRange = (i < b.childLocalMeshletCount);
    if (GTid.x == 0 && b.sourceTag == CLOD_RECORD_SOURCE_REPLAY) {
        WGTelemetryAdd(WG_COUNTER_PHASE2_REPLAY_CLUSTER_BUCKET_RECORDS_CONSUMED, 1);
    }
    WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_THREADS, 1);
    if (inRange) {
        WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_IN_RANGE_THREADS, 1);
    }

    const uint4 allLaneMask = WaveActiveBallot(true);
    const uint allLeaderLane = WaveFirstLaneFromMask(allLaneMask);
    const bool isWaveLeader = (WaveGetLaneIndex() == allLeaderLane);
    const uint inRangeLaneCount = CountBits128(WaveActiveBallot(inRange));
    if (isWaveLeader) {
        WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_WAVES, 1);
        WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_ACTIVE_LANES, inRangeLaneCount);
    }

    uint globalMeshletIndex = 0;
    bool survives = false;

    if (inRange) {
        const bool replaySource = (b.sourceTag == CLOD_RECORD_SOURCE_REPLAY);
        StructuredBuffer<MeshInstanceClodOffsets> clodOffsets =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
        StructuredBuffer<CLodMeshMetadata> clodMeshMetadataBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::MeshMetadata)];
        const MeshInstanceClodOffsets off = clodOffsets[b.instanceIndex];
        const CLodMeshMetadata clodMeshMetadata = clodMeshMetadataBuffer[off.clodMeshMetadataIndex];
        StructuredBuffer<ClusterLODGroup> groups =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
        StructuredBuffer<ClusterLODGroupChunk> groupChunks =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::GroupChunks)];
        StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
        const PerMeshInstanceBuffer instanceData = perMeshInstanceBuffer[b.instanceIndex];
        const uint objectBufferIndex = instanceData.perObjectBufferIndex;
        StructuredBuffer<PerObjectBuffer> perObjectBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
        const row_major matrix objectModelMatrix = perObjectBuffer[objectBufferIndex].model;

        StructuredBuffer<Camera> cameras =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
        const Camera camera = cameras[b.viewId];

        StructuredBuffer<BoundingSphere> meshletBoundsBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::MeshletBounds)];

        if (b.groupId >= clodMeshMetadata.groupChunkTableCount) {
            survives = false;
        }
        else {
        const ClusterLODGroup grp = groups[clodMeshMetadata.groupsBase + b.groupId];
		const ClusterLODGroupChunk groupChunk = groupChunks[clodMeshMetadata.groupChunkTableBase + b.groupId];
        const uint localMeshlet = b.childFirstLocalMeshletIndex + i;
        if (localMeshlet >= groupChunk.meshletCount || localMeshlet >= groupChunk.meshletBoundsCount) {
            survives = false;
        }
        else {
        globalMeshletIndex = groupChunk.meshletBase + localMeshlet;

        const BoundingSphere meshletBounds = meshletBoundsBuffer[groupChunk.meshletBoundsBase + localMeshlet];
        const float3 meshletCenterViewSpace = ToViewSpace(meshletBounds.sphere.xyz, objectModelMatrix, camera.view);
        const float meshletRadiusWorld = meshletBounds.sphere.w * MaxAxisScale_RowVector(objectModelMatrix);

        survives = replaySource || !SphereOutsideFrustumViewSpace(meshletCenterViewSpace, meshletRadiusWorld, camera);

        if (survives && !camera.isOrtho) {
            StructuredBuffer<CLodViewDepthSRVIndex> viewDepthSRVIndices =
                ResourceDescriptorHeap[CLOD_VIEW_DEPTH_SRV_INDICES_DESCRIPTOR_INDEX];
            const uint depthMapDescriptorIndex = viewDepthSRVIndices[b.viewId].linearDepthSRVIndex;
            if (depthMapDescriptorIndex != 0) {
                bool occlusionCulled = false;
                OcclusionCullingPerspectiveTexture2D(
                    occlusionCulled,
                    camera,
                    meshletCenterViewSpace,
                    -meshletCenterViewSpace.z,
                    meshletRadiusWorld,
                    depthMapDescriptorIndex);
                if (occlusionCulled) {
                    if (!replaySource) {
                        ReplayTryAppendMeshlet(
                            b.instanceIndex,
                            b.viewId,
                            b.groupId,
                            b.childFirstLocalMeshletIndex + i);
                    }
                    survives = false;
                }
            }
        }
		}
        }
    }

    const bool contributes = inRange && survives;
    const uint4 survivingMask = WaveActiveBallot(contributes);
    const uint survivingCount = CountBits128(survivingMask);
    if (isWaveLeader) {
        WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_SURVIVING_LANES, survivingCount);
        if (survivingCount == 0) {
            WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_ZERO_SURVIVOR_WAVES, 1);
        }
    }

    if (survivingCount > 0) {
        RWStructuredBuffer<VisibleCluster> visibleClusters =
            ResourceDescriptorHeap[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
        RWStructuredBuffer<uint> visibleClusterCounter =
            ResourceDescriptorHeap[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];
        const uint visibleClusterCapacity = CLOD_VISIBLE_CLUSTERS_CAPACITY;

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

            VisibleCluster vc = (VisibleCluster) 0;
            vc.instanceID = b.instanceIndex;
            vc.globalMeshletIndex = globalMeshletIndex;
            vc.viewID = b.viewId;
            vc.groupID = b.groupId;
            visibleClusters[index] = vc;
        }
    }
}