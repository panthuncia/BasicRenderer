// Compile with DXC target: lib_6_8 (Shader Model 6.8)
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/indirectCommands.hlsli"
// -------------------- Record types --------------------
struct ClodBounds
{
    float4 centerAndRadius; // xyz = center, w = radius
    float  error;   // simplification error in mesh space
    float pad[3];
};

struct ClusterLODChild
{
    int    refinedGroup;              // -1 => terminal meshlets bucket
    uint   firstLocalMeshletIndex;     // into ChildLocalMeshletIndices
    uint   localMeshletCount;
    uint   pad0;
};

struct ClusterLODGroup
{
    ClodBounds bounds;  // center/radius/error
    uint   firstMeshlet;
    uint   meshletCount;
    int    depth;

    uint   firstChild;
    uint   childCount;
    uint2  pad1;
};

// meshopt_Meshlet layout on GPU
struct Meshlet
{
    uint vertex_offset;
    uint triangle_offset;
    uint vertex_count;
    uint triangle_count;
};

struct MeshInstanceClodOffsets
{
    uint groupsBase;
    uint childrenBase;
    uint childLocalMeshletIndicesBase;
    uint meshletsBase;

    uint meshletBoundsBase;
    uint rootGroup;     // group id relative to groupsBase
    uint2 pad;
};

// ----- records -----
struct ObjectCullRecord
{
    uint viewDataIndex; // One record per view, times...
    uint activeDrawSetIndicesSRVIndex; // One record per draw set
    uint activeDrawCount;
	uint pad0; // Padding for 16-byte alignment
    uint3 dispatchGrid : SV_DispatchGrid; // Drives dispatch size
};

struct TraverseRecord
{
    uint instanceIndex;
    uint groupId;    // relative id within this mesh's group array
    uint viewId;
    uint pad;
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

struct MeshletWorkRecord
{
    uint instanceIndex;
    uint meshletId;  // absolute meshlet index into the meshlet buffer (after base)
    uint viewId;
    uint pad;
};

// Node: ObjectCull (entry)
[Shader("node")]
[NodeID("ObjectCull")]
[NodeLaunch("broadcasting")]
[NumThreads(64, 1, 1)]
[NodeMaxDispatchGrid(10000,1,1)]
[NodeIsProgramEntry]
void WG_ObjectCull(
    DispatchNodeInputRecord<ObjectCullRecord> inRec,
    const uint3 vGroupThreadID    : SV_GroupThreadID,
    const uint3 vDispatchThreadID : SV_DispatchThreadID,
    [MaxRecords(64)] NodeOutput<TraverseRecord> Traverse)
{
    const ObjectCullRecord hdr = inRec.Get();
    const bool inRange = (vDispatchThreadID.x < hdr.activeDrawCount);

    // Uniform call; per-thread count may be 0/1.
    ThreadNodeOutputRecords<TraverseRecord> outRecs =
        Traverse.GetThreadNodeOutputRecords(inRange ? 1 : 0);

    if (inRange)
    {
        StructuredBuffer<uint> activeDrawSetIndicesBuffer =
            ResourceDescriptorHeap[hdr.activeDrawSetIndicesSRVIndex];

        StructuredBuffer<DispatchMeshIndirectCommand> indirectCommandBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::IndirectCommandBuffers::Master)];

        const uint drawcallIndex = activeDrawSetIndicesBuffer[vDispatchThreadID.x];
        const uint perMeshInstanceBufferIndex = indirectCommandBuffer[drawcallIndex].perMeshInstanceBufferIndex;

        StructuredBuffer<MeshInstanceClodOffsets> meshInstanceClodOffsets =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];

        TraverseRecord r;
        r.viewId        = hdr.viewDataIndex;
        r.instanceIndex = perMeshInstanceBufferIndex;
        r.groupId       = meshInstanceClodOffsets[perMeshInstanceBufferIndex].rootGroup;
        r.pad           = 0;

        outRecs.Get() = r;
    }

    // Must be uniform even when some threads requested 0 records.
    outRecs.OutputComplete();
}


// Conservative max-axis scale from a row-vector local->world
float MaxAxisScale_RowVector(float4x4 M)
{
    float3 ax = float3(M[0][0], M[0][1], M[0][2]);
    float3 ay = float3(M[1][0], M[1][1], M[1][2]);
    float3 az = float3(M[2][0], M[2][1], M[2][2]);
    return max(length(ax), max(length(ay), length(az)));
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
    // Spec shows both forms depending on example/version:
    //   GroupNodeInputRecords<TraverseRecord, 1> inRecs
    // or:
    [MaxRecords(1)] GroupNodeInputRecords<TraverseRecord> inRecs,

    uint3 gtid : SV_GroupThreadID,

    // Refine jobs (send back to WG_Traverse)
    [MaxRecords(32)] NodeOutput<TraverseRecord> Traverse,

    // Terminal buckets (send to broadcasting meshlet cull)
    [MaxRecords(32)] NodeOutput<MeshletBucketRecord> ClusterCullBuckets
)
{
    const TraverseRecord rec = inRecs[0]; // exactly 1 input record

    StructuredBuffer<MeshInstanceClodOffsets> clodOffsets =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
    StructuredBuffer<ClusterLODGroup> groups =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
    StructuredBuffer<ClusterLODChild> children =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Children)];
    StructuredBuffer<CullingCameraInfo> cameraInfos =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
    StructuredBuffer<PerFrameBuffer> perFrameBuffer =
        ResourceDescriptorHeap[0]; // TODO: proper index, fix across application

    const MeshInstanceClodOffsets off = clodOffsets[rec.instanceIndex];

    const uint groupIndex = off.groupsBase + rec.groupId;
    const ClusterLODGroup grp = groups[groupIndex];
    const uint childBase = off.childrenBase + grp.firstChild;

    // Per-object transform load - TODO: caching, this is redundant across children and the load chain is unnecessaryly deep
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    const uint objectBufferIndex = perMeshInstanceBuffer[rec.instanceIndex].perObjectBufferIndex;
    StructuredBuffer<PerObjectBuffer> perObjectBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    const row_major matrix objectModelMatrix = perObjectBuffer[objectBufferIndex].model;

    const uint ci = gtid.x;
    const bool activeChild = (ci < grp.childCount);

    ClusterLODChild child = (ClusterLODChild)0;
    bool wantsRefine = false;

    if (activeChild)
    {
        child = children[childBase + ci];

        if (child.refinedGroup >= 0)
        {
            const ClusterLODGroup refinedGrp = groups[off.groupsBase + (uint)child.refinedGroup];

            float4 objectSpaceCenter = float4(refinedGrp.bounds.centerAndRadius.xyz, 1.0);
            float3 worldSpaceCenter = mul(objectSpaceCenter, objectModelMatrix).xyz;

            float3 scaleFactors = float3(
                length(objectModelMatrix[0].xyz),
                length(objectModelMatrix[1].xyz),
                length(objectModelMatrix[2].xyz));
            float maxScale = max(max(scaleFactors.x, scaleFactors.y), scaleFactors.z);
            float worldRadius = refinedGrp.bounds.centerAndRadius.w * maxScale;

            const CullingCameraInfo cam = cameraInfos[rec.viewId];

            float px = ProjectedErrorPixels(
                worldSpaceCenter,
                worldRadius,
                refinedGrp.bounds.error,
                cam.positionWorldSpace.xyz,
                cam.projY,
                perFrameBuffer[0].screenResY,
                cam.zNear);

            wantsRefine = (px > cam.errorPixels);
        }
    }

    // Emit refine record (0/1 per child thread)
    {
        const uint n = (activeChild && wantsRefine) ? 1 : 0;

        // Must be called in uniform control flow. :contentReference[oaicite:9]{index=9}
        ThreadNodeOutputRecords<TraverseRecord> o =
            Traverse.GetThreadNodeOutputRecords(n);

        if (n == 1)
        {
            TraverseRecord r;
            r.instanceIndex = rec.instanceIndex;
            r.groupId       = (uint)child.refinedGroup;
            r.viewId        = rec.viewId;
            r.pad           = 0;
            o.Get() = r;
        }

        o.OutputComplete();
    }

    // Emit terminal bucket (0/1 per child thread)
    {
        const bool emitBucket = activeChild && !wantsRefine && (child.localMeshletCount != 0);
        const uint n = emitBucket ? 1 : 0;

        ThreadNodeOutputRecords<MeshletBucketRecord> o =
            ClusterCullBuckets.GetThreadNodeOutputRecords(n);

        if (n == 1)
        {
            // bucket indexes into childLocalMeshletIndices
            const uint idxBase = off.childLocalMeshletIndicesBase + child.firstLocalMeshletIndex;

            MeshletBucketRecord b;
            b.instanceIndex = rec.instanceIndex;
            b.viewId        = rec.viewId;
            b.childLocalMeshletIndexBase = idxBase;
            b.localMeshletCount          = child.localMeshletCount;
            b.meshletsBase               = off.meshletsBase + grp.firstMeshlet;

            // 64 threads per group in next node example:
            const uint groupsX = (b.localMeshletCount + 64 - 1) / 64;
            b.dispatchGrid = uint3(groupsX, 1, 1);

            o.Get() = b;
        }

        o.OutputComplete();
    }
}


// Node: ClusterCull (bin to rasterize vs 2nd pass)
[Shader("node")]
[NodeID("ClusterCullBuckets")]
[NodeLaunch("broadcasting")]
[NumThreads(64, 1, 1)]
[NodeMaxDispatchGrid(65535, 1, 1)] // TODO: Sane max? 2D maybe?
void WG_ClusterCullBuckets(
    DispatchNodeInputRecord<MeshletBucketRecord> inRec,
    uint3 DTid : SV_DispatchThreadID,
    uint3 GTid : SV_GroupThreadID,
    [MaxRecords(64)] NodeOutput<MeshletWorkRecord> Output // raster buckets, 2nd pass, etc.
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

        const uint localMeshlet = childLocalMeshletIndices[b.childLocalMeshletIndexBase + i];
        meshletId = b.meshletsBase + localMeshlet;

        // TODO: culling here (frustum/occlusion/etc)
        survives = true;
    }

    const uint n = (inRange && survives) ? 1 : 0;

    ThreadNodeOutputRecords<MeshletWorkRecord> o =
        Output.GetThreadNodeOutputRecords(n);

    if (n == 1)
    {
        MeshletWorkRecord r;
        r.instanceIndex = b.instanceIndex;
        r.meshletId     = meshletId;
        r.viewId        = b.viewId;
        r.pad           = 0;
        o.Get() = r;
    }

    o.OutputComplete();
}

// Node: Output (stub)
[Shader("node")]
[NodeID("Output")]
[NodeLaunch("coalescing")]
[NumThreads(64, 1, 1)]
void WG_Output(
    [MaxRecords(256)] GroupNodeInputRecords<MeshletWorkRecord> inRecs,
    uint3 gtid : SV_GroupThreadID)
{

}
