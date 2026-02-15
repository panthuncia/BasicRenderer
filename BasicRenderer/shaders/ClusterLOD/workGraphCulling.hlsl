// Compile with DXC target: lib_6_8 (Shader Model 6.8)
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/indirectCommands.hlsli"
#include "include/waveIntrinsicsHelpers.hlsli"
#include "PerPassRootConstants/clodRootConstants.h"

struct MeshInstanceClodOffsets
{
    uint groupsBase;
    uint childrenBase;
    uint childLocalMeshletIndicesBase;
    uint meshletsBase;

    uint meshletBoundsBase;
    uint lodNodesBase;
    uint rootNode; // node index (relative to lodNodesBase) to start traversal from
};
struct ClodBounds
{
    float4 centerAndRadius; // xyz = center, w = radius
    float error; // simplification error in mesh space
    float pad[3];
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

    uint firstChild;
    uint childCount;
    uint terminalChildCount;
    uint pad1;
};

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

static const uint WG_COUNTER_CLUSTER_CULL_THREADS = 19;
static const uint WG_COUNTER_CLUSTER_CULL_IN_RANGE_THREADS = 20;
static const uint WG_COUNTER_CLUSTER_CULL_WAVES = 21;
static const uint WG_COUNTER_CLUSTER_CULL_ACTIVE_LANES = 22;
static const uint WG_COUNTER_CLUSTER_CULL_SURVIVING_LANES = 23;
static const uint WG_COUNTER_CLUSTER_CULL_ZERO_SURVIVOR_WAVES = 24;
static const uint WG_COUNTER_CLUSTER_CULL_VISIBLE_CLUSTER_WRITES = 25;

static const uint WG_COUNTER_TRAVERSE_COALESCED_LAUNCHES = 26;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_RECORDS = 27;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_1 = 28;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_2 = 29;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_3 = 30;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_4 = 31;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_5 = 32;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_6 = 33;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_7 = 34;
static const uint WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_8 = 35;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_LAUNCHES = 36;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_RECORDS = 37;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_1 = 38;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_2 = 39;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_3 = 40;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_4 = 41;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_5 = 42;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_6 = 43;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_7 = 44;
static const uint WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_8 = 45;

static const uint TRAVERSE_THREADS_PER_GROUP = 32;
static const uint TRAVERSE_CHILD_LANES = 4;
static const uint TRAVERSE_RECORDS_PER_GROUP = TRAVERSE_THREADS_PER_GROUP / TRAVERSE_CHILD_LANES;

static const uint GROUP_EVALUATE_THREADS_PER_GROUP = 32;
static const uint GROUP_EVALUATE_CHILD_LANES = 4;
static const uint GROUP_EVALUATE_RECORDS_PER_GROUP = GROUP_EVALUATE_THREADS_PER_GROUP / GROUP_EVALUATE_CHILD_LANES;

void WGTelemetryAdd(uint counterIndex, uint value)
{
    RWStructuredBuffer<uint> telemetryCounters = ResourceDescriptorHeap[CLOD_WORKGRAPH_TELEMETRY_DESCRIPTOR_INDEX];
    InterlockedAdd(telemetryCounters[counterIndex], value);
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
    uint pad0;
};

struct GroupEvalRecord
{
    uint instanceIndex;
    uint groupId;
    uint viewId;
    uint pad0;
};

struct MeshletBucketRecord
{
    uint instanceIndex;
    uint viewId;

    uint groupId;
    uint childFirstLocalMeshletIndex;
    uint childLocalMeshletCount;

    uint3 dispatchGrid : SV_DispatchGrid; // drives broadcasting node launch
};

// struct MeshletWorkRecord
// {
//     uint instanceIndex;
//     uint meshletId;  // absolute meshlet index into the meshlet buffer (after base)
//     uint viewId;
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

            const MeshInstanceClodOffsets off = meshInstanceClodOffsets[perMeshInstanceBufferIndex];

            outRecord.viewId = hdr.viewDataIndex;
            outRecord.instanceIndex =perMeshInstanceBufferIndex;
            outRecord.nodeId = off.rootNode;   // BVH root node for this mesh
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

// clusterlod.h comment formula, converted to pixels:
// projectedErrorPx = (error / max(dist - radius, znear)) * (projY * 0.5) * viewportHeight
float ProjectedErrorPixels(float3 worldCenter, float worldRadius, float errorMeshSpace, float3 cameraPos, float projY, float screenHeight, float zNear) {
    // Conservative "distance to sphere surface"
    float dist = length(worldCenter - cameraPos);
    float denom = max(dist - worldRadius, zNear);

    // bounds.error / denom * (projY * 0.5) * screenHeight
    float frac = (errorMeshSpace / denom) * (projY * 0.5f);
    return frac * screenHeight;
}

struct TraverseSlotState
{
    uint emitChildren;
    uint emitGroup;
    uint childCount;
    uint childBase;
    uint instanceIndex;
    uint viewId;
    uint groupId;
};

groupshared TraverseSlotState g_slotState[TRAVERSE_RECORDS_PER_GROUP];

struct GroupEvaluateSlotState
{
    uint active;
    uint instanceIndex;
    uint groupId;
    uint viewId;
    uint childBase;
    uint clampedChildCount;
    uint terminalChildCount;
};

groupshared GroupEvaluateSlotState g_groupSlotState[GROUP_EVALUATE_RECORDS_PER_GROUP];

// Node: TraverseNodes (recursive, BVH-only)
[Shader("node")]
[NodeID("TraverseNodes")]
[NodeLaunch("coalescing")]
[NumThreads(TRAVERSE_THREADS_PER_GROUP, 1, 1)]
[NodeMaxRecursionDepth(25)]
void WG_TraverseNodes(
    [MaxRecords(TRAVERSE_RECORDS_PER_GROUP)] GroupNodeInputRecords<TraverseNodeRecord> inRecs,
    uint3 gtid : SV_GroupThreadID,
    [MaxRecords(32)] NodeOutput<TraverseNodeRecord> TraverseNodes,
    [MaxRecords(TRAVERSE_RECORDS_PER_GROUP)] NodeOutput<GroupEvalRecord> GroupEvaluate) {
    const uint lane = gtid.x % TRAVERSE_CHILD_LANES;
    const uint slot = gtid.x / TRAVERSE_CHILD_LANES;
    const uint inputCount = inRecs.Count();
    const bool slotActive = slot < inputCount;

    WGTelemetryAdd(WG_COUNTER_TRAVERSE_THREADS, 1);
    if (lane == 0) {
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_COALESCED_LAUNCHES, 1);
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_COALESCED_INPUT_RECORDS, inputCount);
        if (inputCount > 0 && inputCount <= TRAVERSE_RECORDS_PER_GROUP) {
            WGTelemetryAdd(WG_COUNTER_TRAVERSE_COALESCED_INPUT_COUNT_1 + (inputCount - 1), 1);
        }

        TraverseSlotState s = (TraverseSlotState) 0;

        if (slotActive) {
            const TraverseNodeRecord rec = inRecs[slot];

            StructuredBuffer<MeshInstanceClodOffsets> clodOffsets =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
            const MeshInstanceClodOffsets off = clodOffsets[rec.instanceIndex];
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
            ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

            StructuredBuffer<ClusterLODNode> lodNodes =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Nodes)];

            const ClusterLODNode node = lodNodes[off.lodNodesBase + rec.nodeId];

            if (node.range.isGroup == 0) {
                WGTelemetryAdd(WG_COUNTER_TRAVERSE_INTERNAL_NODE_RECORDS, 1);
            }
            else {
                WGTelemetryAdd(WG_COUNTER_TRAVERSE_LEAF_NODE_RECORDS, 1);
            }

            const float3 nodeCenterObjectSpace = node.metric.centerAndRadius.xyz;
            const float3 nodeCenterViewSpace = ToViewSpace(nodeCenterObjectSpace, objectModelMatrix, camera.view);
            const float nodeRadiusWorld = node.metric.centerAndRadius.w * MaxAxisScale_RowVector(objectModelMatrix);
            const bool nodeCulled = SphereOutsideFrustumViewSpace(nodeCenterViewSpace, nodeRadiusWorld, camera);

            if (nodeCulled) {
                WGTelemetryAdd(WG_COUNTER_TRAVERSE_CULLED_NODE_RECORDS, 1);
            }
            else {
                float4 nodeCenterObjectSpace4 = float4(nodeCenterObjectSpace, 1.0f);
                float3 nodeCenterWorldSpace = mul(nodeCenterObjectSpace4, objectModelMatrix).xyz;
                float nodeProjectedError = ProjectedErrorPixels(
                    nodeCenterWorldSpace,
                    nodeRadiusWorld,
                    node.metric.maxQuadricError,
                    cam.positionWorldSpace.xyz,
                    cam.projY,
                    perFrameBuffer.screenResY,
                    cam.zNear);

                const bool nodeWantsTraversal = nodeProjectedError > cam.errorPixels;
                if (!nodeWantsTraversal) {
                    WGTelemetryAdd(WG_COUNTER_TRAVERSE_REJECTED_BY_ERROR_RECORDS, 1);
                }
                else if (node.range.isGroup == 0) {
                    s.emitChildren = 1;
                    s.childCount = min(node.range.countMinusOne + 1, TRAVERSE_CHILD_LANES);
                    s.childBase = node.range.indexOrOffset;
                    s.instanceIndex = rec.instanceIndex;
                    s.viewId = rec.viewId;
                }
                else {
                    s.emitGroup = 1;
                    s.instanceIndex = rec.instanceIndex;
                    s.viewId = rec.viewId;
                    s.groupId = node.range.indexOrOffset;
                }
            }
        }

        g_slotState[slot] = s;
    }

    GroupMemoryBarrierWithGroupSync();

    const TraverseSlotState s = g_slotState[slot];

    const bool emitTraverse = slotActive && (s.emitChildren != 0) && (lane < s.childCount);
    if (emitTraverse) {
        WGTelemetryAdd(WG_COUNTER_TRAVERSE_ACTIVE_CHILD_THREADS, 1);
    }

    const bool emitGroup = slotActive && (lane == 0) && (s.emitGroup != 0);

    ThreadNodeOutputRecords<TraverseNodeRecord> outNodes =
        TraverseNodes.GetThreadNodeOutputRecords(emitTraverse ? 1 : 0);
    ThreadNodeOutputRecords<GroupEvalRecord> outGroups =
        GroupEvaluate.GetThreadNodeOutputRecords(emitGroup ? 1 : 0);

    if (emitTraverse) {
        TraverseNodeRecord r;
        r.instanceIndex = s.instanceIndex;
        r.viewId = s.viewId;
        r.nodeId = s.childBase + lane;
        outNodes.Get() = r;

        WGTelemetryAdd(WG_COUNTER_TRAVERSE_TRAVERSE_RECORDS, 1);
    }

    if (emitGroup) {
        GroupEvalRecord r;
        r.instanceIndex = s.instanceIndex;
        r.viewId = s.viewId;
        r.groupId = s.groupId;
        outGroups.Get() = r;

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
[NumThreads(GROUP_EVALUATE_THREADS_PER_GROUP, 1, 1)]
void WG_GroupEvaluate(
    [MaxRecords(GROUP_EVALUATE_RECORDS_PER_GROUP)] GroupNodeInputRecords<GroupEvalRecord> inRecs,
    uint3 gtid : SV_GroupThreadID,
    [MaxRecords(GROUP_EVALUATE_RECORDS_PER_GROUP * GROUP_EVALUATE_CHILD_LANES)] NodeOutput<MeshletBucketRecord> ClusterCullBuckets) {
    const uint lane = gtid.x % GROUP_EVALUATE_CHILD_LANES;
    const uint slot = gtid.x / GROUP_EVALUATE_CHILD_LANES;
    const uint inputCount = inRecs.Count();
    const bool slotActive = slot < inputCount;

    WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_THREADS, 1);

    if (lane == 0) {
        if (slot == 0) {
            WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_COALESCED_LAUNCHES, 1);
            WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_RECORDS, inputCount);
            if (inputCount > 0 && inputCount <= GROUP_EVALUATE_RECORDS_PER_GROUP) {
                WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_COALESCED_INPUT_COUNT_1 + (inputCount - 1), 1);
            }
        }

        GroupEvaluateSlotState s = (GroupEvaluateSlotState) 0;

        if (slotActive) {
            const GroupEvalRecord rec = inRecs[slot];
            WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_GROUP_RECORDS, 1);

            StructuredBuffer<MeshInstanceClodOffsets> clodOffsets =
                        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
            const MeshInstanceClodOffsets off = clodOffsets[rec.instanceIndex];
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
            ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

            StructuredBuffer<ClusterLODGroup> groups =
                        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];

            const uint groupIndex = off.groupsBase + rec.groupId;
            const ClusterLODGroup grp = groups[groupIndex];

            const float3 groupCenterObjectSpace = grp.bounds.centerAndRadius.xyz;
            const float3 groupCenterViewSpace = ToViewSpace(groupCenterObjectSpace, objectModelMatrix, camera.view);
            const float groupRadiusWorld = grp.bounds.centerAndRadius.w * MaxAxisScale_RowVector(objectModelMatrix);
            const bool groupCulled = SphereOutsideFrustumViewSpace(groupCenterViewSpace, groupRadiusWorld, camera);

            if (groupCulled) {
                WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_CULLED_GROUP_RECORDS, 1);
            }
            else {
                float4 groupCenterObjectSpace4 = float4(groupCenterObjectSpace, 1.0f);
                float3 groupCenterWorldSpace = mul(groupCenterObjectSpace4, objectModelMatrix).xyz;
                float groupProjectedError = ProjectedErrorPixels(
                    groupCenterWorldSpace,
                    groupRadiusWorld,
                    grp.bounds.error,
                    cam.positionWorldSpace.xyz,
                    cam.projY,
                    perFrameBuffer.screenResY,
                    cam.zNear);

                const bool groupWantsTraversal = groupProjectedError > cam.errorPixels;
                if (!groupWantsTraversal) {
                    WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_REJECTED_BY_ERROR_RECORDS, 1);
                }
                else {
                    s.active = 1;
                    s.instanceIndex = rec.instanceIndex;
                    s.groupId = rec.groupId;
                    s.viewId = rec.viewId;
                    s.childBase = off.childrenBase + grp.firstChild;
                    s.terminalChildCount = min(grp.terminalChildCount, GROUP_EVALUATE_CHILD_LANES);
                    s.clampedChildCount = min(grp.childCount, GROUP_EVALUATE_CHILD_LANES);
                }
            }
        }

        g_groupSlotState[slot] = s;
    }

    GroupMemoryBarrierWithGroupSync();

    const GroupEvaluateSlotState s = g_groupSlotState[slot];
    const bool activeChild = slotActive && (s.active != 0) && (lane < s.clampedChildCount);
    if (activeChild) {
        WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_ACTIVE_CHILD_THREADS, 1);
    }

    ClusterLODChild child = (ClusterLODChild) 0;
    bool generatingGroupWantsTraversal = false;
    bool forceBucket = false;

    if (activeChild) {
        StructuredBuffer<MeshInstanceClodOffsets> clodOffsets =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
        const MeshInstanceClodOffsets off = clodOffsets[s.instanceIndex];
        StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
        const uint objectBufferIndex = perMeshInstanceBuffer[s.instanceIndex].perObjectBufferIndex;
        StructuredBuffer<PerObjectBuffer> perObjectBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
        const row_major matrix objectModelMatrix = perObjectBuffer[objectBufferIndex].model;
        StructuredBuffer<Camera> cameras =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
        const Camera camera = cameras[s.viewId];
        StructuredBuffer<CullingCameraInfo> cameraInfos =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
        const CullingCameraInfo cam = cameraInfos[s.viewId];
        ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

        StructuredBuffer<ClusterLODGroup> groups =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
        StructuredBuffer<ClusterLODChild> children =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Children)];

        child = children[s.childBase + lane];
        forceBucket = (lane < s.terminalChildCount) || (child.refinedGroup < 0);

        if (!forceBucket && child.refinedGroup >= 0) {
            const ClusterLODGroup refinedGrp = groups[off.groupsBase + (uint) child.refinedGroup];

            float4 objectSpaceCenter = float4(refinedGrp.bounds.centerAndRadius.xyz, 1.0);
            float3 worldSpaceCenter = mul(objectSpaceCenter, objectModelMatrix).xyz;

            float worldRadius = refinedGrp.bounds.centerAndRadius.w * MaxAxisScale_RowVector(objectModelMatrix);
            float3 refinedViewSpaceCenter = mul(float4(worldSpaceCenter, 1.0f), camera.view).xyz;
            const bool refinedCulled = SphereOutsideFrustumViewSpace(refinedViewSpaceCenter, worldRadius, camera);

            float px = ProjectedErrorPixels(
                worldSpaceCenter,
                worldRadius,
                refinedGrp.bounds.error,
                cam.positionWorldSpace.xyz,
                cam.projY,
                perFrameBuffer.screenResY,
                cam.zNear);

            generatingGroupWantsTraversal = !refinedCulled && (px > cam.errorPixels);
            if (generatingGroupWantsTraversal) {
                WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_REFINED_TRAVERSAL_THREADS, 1);
            }
        }
    }

    // Emit one record per terminal child bucket.
    const bool emitBucket = activeChild && (child.localMeshletCount != 0)
        && (forceBucket || !generatingGroupWantsTraversal);
    if (emitBucket) {
        WGTelemetryAdd(WG_COUNTER_GROUP_EVALUATE_EMIT_BUCKET_THREADS, 1);
    }

    ThreadNodeOutputRecords<MeshletBucketRecord> outBuckets =
        ClusterCullBuckets.GetThreadNodeOutputRecords(emitBucket ? 1 : 0);

    if (emitBucket) {
        MeshletBucketRecord b;
        b.instanceIndex = s.instanceIndex;
        b.viewId = s.viewId;
        b.groupId = s.groupId;
        b.childFirstLocalMeshletIndex = child.firstLocalMeshletIndex;
        b.childLocalMeshletCount = child.localMeshletCount;

        const uint groupsX = (b.childLocalMeshletCount + CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP - 1) / CLUSTER_CULL_BUCKETS_THREADS_PER_GROUP;
        b.dispatchGrid = uint3(groupsX, 1, 1);

        outBuckets.Get() = b;
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

    uint meshletId = 0;
    bool survives = false;

    if (inRange) {
        StructuredBuffer<MeshInstanceClodOffsets> clodOffsets =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
        const MeshInstanceClodOffsets off = clodOffsets[b.instanceIndex];

        StructuredBuffer<ClusterLODGroup> groups =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];

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

        const ClusterLODGroup grp = groups[off.groupsBase + b.groupId];
        const uint localMeshlet = b.childFirstLocalMeshletIndex + i;
        meshletId = grp.firstMeshlet + localMeshlet;

        const BoundingSphere meshletBounds = meshletBoundsBuffer[meshletId];
        const float3 meshletCenterViewSpace = ToViewSpace(meshletBounds.sphere.xyz, objectModelMatrix, camera.view);
        const float meshletRadiusWorld = meshletBounds.sphere.w * MaxAxisScale_RowVector(objectModelMatrix);

        survives = !SphereOutsideFrustumViewSpace(meshletCenterViewSpace, meshletRadiusWorld, camera);
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

        const uint leaderLane = WaveFirstLaneFromMask(survivingMask);
        const uint laneRank = GetLaneRankInGroup(survivingMask, WaveGetLaneIndex());

        uint baseIndex = 0;
        if (WaveGetLaneIndex() == leaderLane) {
            InterlockedAdd(visibleClusterCounter[0], survivingCount, baseIndex);
        }

        baseIndex = WaveReadLaneAt(baseIndex, leaderLane);

        if (isWaveLeader) {
            WGTelemetryAdd(WG_COUNTER_CLUSTER_CULL_VISIBLE_CLUSTER_WRITES, survivingCount);
        }

        if (contributes) {
            const uint index = baseIndex + laneRank;

            VisibleCluster vc = (VisibleCluster) 0;
            vc.instanceID = b.instanceIndex;
            vc.meshletID = meshletId;
            vc.viewID = b.viewId;
            visibleClusters[index] = vc;
        }
    }
}