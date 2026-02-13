// Compile with DXC target: lib_6_8 (Shader Model 6.8)
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/indirectCommands.hlsli"
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
    uint firstLocalMeshletIndex; // into ChildLocalMeshletIndices
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
    uint2 pad1;
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

// ----- records -----
struct ObjectCullRecord
{
    uint viewDataIndex; // One record per view, times...
    uint activeDrawSetIndicesSRVIndex; // One record per draw set
    uint activeDrawCount;
    uint3 dispatchGrid : SV_DispatchGrid; // Drives dispatch size
};

struct TraverseRecord
{
    uint instanceIndex;
    uint id; // nodeId OR groupId depending on kind
    uint viewId;
    uint kind; // 0 = BVH node, 1 = Group
};

struct MeshletBucketRecord
{
    uint instanceIndex;
    uint viewId;

    // Absolute base into childLocalMeshletIndices for this bucket:
    uint childLocalMeshletIndexBase;
    uint localMeshletCount;

    // Absolute base for final meshlet IDs (added to each local meshlet):
    uint meshletsBase;

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
    [MaxRecords(64)] NodeOutput<TraverseRecord> Traverse)
{
const ObjectCullRecord hdr = inRec.Get();
const bool inRange = (vDispatchThreadID.x < hdr.activeDrawCount);

uint outCount = 0;
TraverseRecord outRecord = (TraverseRecord) 0;

    if (inRange)
    {
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
        if (!culled)
        {
StructuredBuffer<MeshInstanceClodOffsets> meshInstanceClodOffsets =
                ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];

const MeshInstanceClodOffsets off = meshInstanceClodOffsets[perMeshInstanceBufferIndex];

            outRecord.viewId        = hdr.
viewDataIndex;
            outRecord.instanceIndex =
perMeshInstanceBufferIndex;
            outRecord.id            = off.
rootNode;   // BVH root node for this mesh
            outRecord.kind          = 0;              // Node
            outCount = 1;
        }
    }

    // Uniform call; per-thread count may be 0/1.
    ThreadNodeOutputRecords<TraverseRecord> outRecs =
        Traverse.GetThreadNodeOutputRecords(outCount);

    if (outCount == 1)
    {
        outRecs.Get() =
outRecord;
    }

    // Must be uniform even when some threads requested 0 records.
    outRecs.OutputComplete();
}

// clusterlod.h comment formula, converted to pixels:
// projectedErrorPx = (error / max(dist - radius, znear)) * (projY * 0.5) * viewportHeight
float ProjectedErrorPixels(float3 worldCenter, float worldRadius, float errorMeshSpace, float3 cameraPos, float projY, float screenHeight, float zNear)
{
    // Conservative "distance to sphere surface"
    float dist = length(worldCenter - cameraPos);
    float denom = max(dist - worldRadius, zNear);

    // bounds.error / denom * (projY * 0.5) * screenHeight
    float frac = (errorMeshSpace / denom) * (projY * 0.5f);
    return frac * screenHeight;
}

// Node: Traverse (recursive)
[Shader("node")]
[NodeID("Traverse")]
[NodeLaunch("coalescing")]
[NumThreads(32, 1, 1)]
[NodeMaxRecursionDepth(25)]
void WG_Traverse(
    [MaxRecords(1)] GroupNodeInputRecords<TraverseRecord> inRecs,
uint3 gtid : SV_GroupThreadID,
    [ MaxRecords(32)] NodeOutput<TraverseRecord> Traverse,
    [MaxRecords(32)] NodeOutput<MeshletBucketRecord> ClusterCullBuckets
)
{
const TraverseRecord rec = inRecs[0];

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
ConstantBuffer<PerFrameBuffer> perFrameBuffer =
        ResourceDescriptorHeap[0]; // TODO: fix

    // ----------------------------
    // Case A: BVH node expansion
    // ----------------------------
    if (rec.kind == 0) // Node
    {
StructuredBuffer<ClusterLODNode> lodNodes =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Nodes)];

const ClusterLODNode node = lodNodes[off.lodNodesBase + rec.id];

const float3 nodeCenterObjectSpace = node.metric.centerAndRadius.xyz;
const float3 nodeCenterViewSpace = ToViewSpace(nodeCenterObjectSpace, objectModelMatrix, camera.view);
const float nodeRadiusWorld = node.metric.centerAndRadius.w * MaxAxisScale_RowVector(objectModelMatrix);
const bool nodeCulled = SphereOutsideFrustumViewSpace(nodeCenterViewSpace, nodeRadiusWorld, camera);

        if (nodeCulled)
        {
            ThreadNodeOutputRecords<TraverseRecord> o =
                Traverse.GetThreadNodeOutputRecords(0);
            o.OutputComplete();
            return;
        }

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
        if (!nodeWantsTraversal)
        {
            ThreadNodeOutputRecords<TraverseRecord> o =
                Traverse.GetThreadNodeOutputRecords(0);
            o.OutputComplete();
            return;
        }

        // Internal BVH node: emit up to 32 children as new node records
        if (node.range.isGroup == 0)
        {
const uint ci = gtid.x;
const uint childCount = node.range.countMinusOne + 1;

const bool activeChild = (ci < childCount);

            ThreadNodeOutputRecords<TraverseRecord> o =
                Traverse.GetThreadNodeOutputRecords(activeChild ? 1 : 0);

            if (activeChild)
            {
TraverseRecord r;
                r.instanceIndex = rec.
instanceIndex;
                r.viewId        = rec.
viewId;
                r.kind          = 0; // Node
                r.id            = node.range.indexOrOffset +
ci; // child node id (relative to lodNodesBase)
                o.Get() =
r;
            }
            o.OutputComplete();
            return;
        }

        // Leaf BVH node that references a group:
        // emit exactly 1 group record so the *group* code path can run with 32 threads
        if (gtid.x == 0)
        {
            ThreadNodeOutputRecords<TraverseRecord> o =
                Traverse.GetThreadNodeOutputRecords(1);

TraverseRecord r;
            r.instanceIndex = rec.
instanceIndex;
            r.viewId        = rec.
viewId;
            r.kind          = 1; // Group
            r.id            = node.range.
indexOrOffset; // groupId (relative to groupsBase)
            o.Get() =
r;
            o.OutputComplete();
        }
        else
        {
            // Must still call uniformly
            ThreadNodeOutputRecords<TraverseRecord> o =
                Traverse.GetThreadNodeOutputRecords(0);
            o.OutputComplete();
        }
        return;
    }

    // ----------------------------
    // Case B: Group evaluation
    // ----------------------------
    {
const uint groupId = rec.id;

StructuredBuffer<ClusterLODGroup> groups =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
StructuredBuffer<ClusterLODChild> children =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Children)];

const uint groupIndex = off.groupsBase + groupId;
const ClusterLODGroup grp = groups[groupIndex];
const uint childBase = off.childrenBase + grp.firstChild;

const float3 groupCenterObjectSpace = grp.bounds.centerAndRadius.xyz;
const float3 groupCenterViewSpace = ToViewSpace(groupCenterObjectSpace, objectModelMatrix, camera.view);
const float groupRadiusWorld = grp.bounds.centerAndRadius.w * MaxAxisScale_RowVector(objectModelMatrix);
const bool groupCulled = SphereOutsideFrustumViewSpace(groupCenterViewSpace, groupRadiusWorld, camera);

        if (groupCulled)
        {
            ThreadNodeOutputRecords<TraverseRecord> noTraverse =
                Traverse.GetThreadNodeOutputRecords(0);
            noTraverse.OutputComplete();

            ThreadNodeOutputRecords<MeshletBucketRecord> noBuckets =
                ClusterCullBuckets.GetThreadNodeOutputRecords(0);
            noBuckets.OutputComplete();
            return;
        }

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
        if (!groupWantsTraversal)
        {
            ThreadNodeOutputRecords<TraverseRecord> noTraverse =
                Traverse.GetThreadNodeOutputRecords(0);
            noTraverse.OutputComplete();

            ThreadNodeOutputRecords<MeshletBucketRecord> noBuckets =
                ClusterCullBuckets.GetThreadNodeOutputRecords(0);
            noBuckets.OutputComplete();
            return;
        }

const uint ci = gtid.x;
const bool activeChild = (ci < grp.childCount);

ClusterLODChild child = (ClusterLODChild) 0;
bool generatingGroupWantsTraversal = false;
bool hasGeneratingGroup = false;

        if (activeChild)
        {
            child = children[childBase + ci];

            if (child.refinedGroup >= 0)
            {
                hasGeneratingGroup = true;

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
            }
        }

        // NVIDIA-style cut selection:
        // emit current group's bucket if this group's metric passed and generating group's metric did NOT pass.
        // Buckets with refinedGroup < 0 are highest detail and are forced.
        {
            ThreadNodeOutputRecords<TraverseRecord> noTraverse =
                Traverse.GetThreadNodeOutputRecords(0);
            noTraverse.OutputComplete();
        }

        // Emit terminal bucket(s)
        {
const bool forceBucket = !hasGeneratingGroup;
bool emitBucket = activeChild && (child.localMeshletCount != 0)
                           && (forceBucket || !generatingGroupWantsTraversal);

const uint n = emitBucket ? 1 : 0;

            ThreadNodeOutputRecords<MeshletBucketRecord> o =
                ClusterCullBuckets.GetThreadNodeOutputRecords(n);

            if (n == 1)
            {
const uint idxBase = off.childLocalMeshletIndicesBase + child.firstLocalMeshletIndex;

MeshletBucketRecord b;
                b.instanceIndex = rec.
instanceIndex;
                b.viewId        = rec.
viewId;
                b.childLocalMeshletIndexBase =
idxBase;
                b.localMeshletCount          = child.
localMeshletCount;
                b.meshletsBase               = grp.
firstMeshlet;

const uint groupsX = (b.localMeshletCount + 64 - 1) / 64;
                b.dispatchGrid = uint3(groupsX, 1, 1);

                o.Get() =
b;
            }

            o.OutputComplete();
        }
    }
}



// Node: ClusterCull (bin to rasterize vs 2nd pass)
[Shader("node")]
[NodeID("ClusterCullBuckets")]
[NodeLaunch("broadcasting")]
[NumThreads(64, 1, 1)]
[NodeMaxDispatchGrid(65535, 1, 1)] // TODO: Sane max? 2D maybe?
void WG_ClusterCullBuckets(
    DispatchNodeInputRecord< MeshletBucketRecord> inRec,
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID
    //[MaxRecords(64)] NodeOutput<MeshletWorkRecord> Output // raster buckets, 2nd pass, etc.
)
{
    const MeshletBucketRecord b = inRec.Get();
    const uint i = DTid.x;

    const bool inRange = (i < b.localMeshletCount);

    uint meshletId = 0;
    bool survives = false;

    if (inRange)
    {
        StructuredBuffer<uint> childLocalMeshletIndices =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::ChildLocalMeshletIndices)];

        StructuredBuffer<MeshInstanceClodOffsets> clodOffsets =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
        const MeshInstanceClodOffsets off = clodOffsets[b.instanceIndex];

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

        const uint localMeshlet = childLocalMeshletIndices[b.childLocalMeshletIndexBase + i];
        meshletId = b.meshletsBase + localMeshlet;

        const BoundingSphere meshletBounds = meshletBoundsBuffer[meshletId]; // NOTE: This depends on the bounds buffer being indexed the same way as meshlets; would there ever be a reason for it not to be?
        const float3 meshletCenterViewSpace = ToViewSpace(meshletBounds.sphere.xyz, objectModelMatrix, camera.view);
        const float meshletRadiusWorld = meshletBounds.sphere.w * MaxAxisScale_RowVector(objectModelMatrix);

        survives = !SphereOutsideFrustumViewSpace(meshletCenterViewSpace, meshletRadiusWorld, camera);
    }

    const uint n = (inRange && survives) ? 1 : 0;

    if (n == 1)
    {
        RWStructuredBuffer<VisibleCluster> visibleClusters =
            ResourceDescriptorHeap[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
        RWStructuredBuffer<uint> visibleClusterCounter =
            ResourceDescriptorHeap[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];
        
        // Atomically get an index to write to
        uint index = 0;
        InterlockedAdd(visibleClusterCounter[0], 1, index);

        VisibleCluster vc = (VisibleCluster) 0;
        vc.instanceID = b.instanceIndex;
        vc.meshletID = meshletId;
        vc.viewID = b.viewId;
        visibleClusters[index] = vc;
    }
}