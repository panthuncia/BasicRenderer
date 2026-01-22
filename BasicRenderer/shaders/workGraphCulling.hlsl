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

struct MeshletWorkRecord
{
    uint instanceIndex;
    uint meshletId;  // absolute meshlet index into the meshlet buffer (after base)
    uint viewId;
    uint pad;
};

struct ClusterRecord
{
    uint viewDataIndex;
    uint perMeshInstanceIndex;
    uint meshletGlobalIndex;
    uint pad0;
};

struct RasterRecord
{
    uint perMeshInstanceIndex;
    uint meshletGlobalIndex;
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
    const uint3 vGroupThreadID : SV_GroupThreadID,
    const uint3 vDispatchThreadID : SV_DispatchThreadID,
    [MaxRecords(64)] NodeOutput<TraverseRecord> Traverse)
{
    if (vDispatchThreadID.x >= inRec.Get().activeDrawCount){
        return;
    }
    // Stub: everything survives culling
    StructuredBuffer<unsigned int> activeDrawSetIndicesBuffer = ResourceDescriptorHeap[inRec.Get().activeDrawSetIndicesSRVIndex];
    StructuredBuffer<DispatchMeshIndirectCommand> indirectCommandBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::IndirectCommandBuffers::Master)];
    // Determine which drawcall this thread is processing
    uint drawcallIndex = activeDrawSetIndicesBuffer[vDispatchThreadID.x];
    uint perMeshInstanceBufferIndex = indirectCommandBuffer[drawcallIndex].perMeshInstanceBufferIndex;
    StructuredBuffer<MeshInstanceClodOffsets> meshInstanceClodOffsets = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
    //StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    
    // call must be uniform; outCount may be non-uniform.
    uint outIndex = vGroupThreadID.x;
    ThreadNodeOutputRecords<TraverseRecord> outRecord = Traverse.GetThreadNodeOutputRecords(1); // TODO: Is this right?

    outRecord[0].viewId = inRec.Get().viewDataIndex;
    outRecord[0].instanceIndex = perMeshInstanceBufferIndex;
    outRecord[0].groupId = meshInstanceClodOffsets[perMeshInstanceBufferIndex].rootGroup;

    outRecord.OutputComplete();
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
[NodeLaunch("thread")]
[NodeMaxRecursionDepth(25)] // Max depth of 32 for the whole graph
void WG_Traverse(
    ThreadNodeInputRecord<TraverseRecord> inRec,

    // Refine jobs (send back to WG_Traverse)
    NodeOutput<TraverseRecord> refineOut,

    // Selected meshlets (send to meshlet cull / raster node)
    NodeOutput<MeshletWorkRecord> meshletOut
)
{
    // TODO: packing
    StructuredBuffer<MeshInstanceClodOffsets> clodOffsets = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Offsets)];
    StructuredBuffer<ClusterLODGroup>         groups = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Groups)];
    StructuredBuffer<ClusterLODChild>         children = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Children)];
    StructuredBuffer<uint32_t>                childLocalMeshletIndices = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::ChildLocalMeshletIndices)];
    StructuredBuffer<CullingCameraInfo>       cameraInfos = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
    StructuredBuffer<PerFrameBuffer>          perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)]; // TODO: Does loading one field from this load the whole struct?

    //StructuredBuffer<Meshlet>                 meshlets = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::Meshlets)];
    //StructuredBuffer<float4>                  meshletBounds = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::MeshletBounds)];
    TraverseRecord rec = inRec.Get();

    MeshInstanceClodOffsets off = clodOffsets[rec.instanceIndex];

    // Load group
    uint groupIndex = off.groupsBase + rec.groupId;
    ClusterLODGroup grp = groups[groupIndex];
    // group-level visibility cull here?
    // If group sphere is outside frustum / HZB occluded, just return without emitting anything.

    // Iterate child buckets (each bucket is one (group->refinedGroup) edge, plus the -1 terminal bucket)
    uint childBase = off.childrenBase + grp.firstChild;

    // TODO: This load chain is probably unnecessary- restructure.
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    uint objectBufferIndex = perMeshInstanceBuffer[rec.instanceIndex].perObjectBufferIndex;
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    row_major matrix objectModelMatrix = perObjectBuffer[objectBufferIndex].model;

    [loop]
    for (uint ci = 0; ci < grp.childCount; ++ci)
    {
        ClusterLODChild child = children[childBase + ci];

        bool wantsRefine = false;

        if (child.refinedGroup >= 0)
        {
            // Use the *refined group's* error as the metric for whether the parent's representation is acceptable.
            ClusterLODGroup refinedGrp = groups[off.groupsBase + (uint)child.refinedGroup];

            // Transform refined group bounds center/radius to world.
            float4 objectSpaceCenter = float4(refinedGrp.bounds.centerAndRadius.xyz, 1.0); // TODO: Can we omit rotation here?
            float3 worldSpaceCenter = mul(objectSpaceCenter, objectModelMatrix).xyz;
            
            float3 scaleFactors = float3(
            length(objectModelMatrix[0].xyz),
            length(objectModelMatrix[1].xyz),
            length(objectModelMatrix[2].xyz));
            float maxScale = max(max(scaleFactors.x, scaleFactors.y), scaleFactors.z);
            float  worldRadius = refinedGrp.bounds.centerAndRadius.w * maxScale;

            CullingCameraInfo cam = cameraInfos[rec.viewId];

            float px = ProjectedErrorPixels(worldSpaceCenter, worldRadius, refinedGrp.bounds.error, cam.positionWorldSpace.xyz, cam.projY, perFrameBuffer[0].screenResY, cam.zNear);

            // If error exceeds threshold, we need the finer representation => traverse into refinedGroup.
            wantsRefine = (px > cam.errorPixels);
        }

        // emit refine record (0 or 1)
        // Allocation must not be branched around; use the bool-gated allocator pattern.
        {
            bool alloc = wantsRefine;
            ThreadNodeOutputRecords<TraverseRecord> o =
                refineOut.GetThreadNodeOutputRecords(alloc);

            if (alloc)
            {
                o.Get().instanceIndex = rec.instanceIndex;
                o.Get().groupId       = (uint)child.refinedGroup;
                o.Get().viewId        = rec.viewId;
                o.Get().pad           = 0;
            }

            o.OutputComplete();
        }

        // if NOT refining this bucket, emit meshlets belonging to it
        if (!wantsRefine)
        {
            uint idxBase = off.childLocalMeshletIndicesBase + child.firstLocalMeshletIndex;

            [loop]
            for (uint mi = 0; mi < child.localMeshletCount; ++mi)
            {
                uint localMeshlet = (uint)childLocalMeshletIndices[idxBase + mi];
                uint meshletId    = off.meshletsBase + grp.firstMeshlet + localMeshlet;

                // Next node will cull meshlet

                bool alloc = true;
                ThreadNodeOutputRecords<MeshletWorkRecord> mo =
                    meshletOut.GetThreadNodeOutputRecords(alloc);

                // Fill
                mo.Get().instanceIndex = rec.instanceIndex;
                mo.Get().meshletId     = meshletId;
                mo.Get().viewId        = rec.viewId;
                mo.Get().pad           = 0;

                mo.OutputComplete();
            }
        }
    }
}


// Node: ClusterCull (bin to rasterize vs 2nd pass)
[Shader("node")]
[NodeID("ClusterCull")]
[NodeLaunch("coalescing")]
[NumThreads(64, 1, 1)]
void WG_ClusterCull(
    [MaxRecords(256)] GroupNodeInputRecords<ClusterRecord> inRecs, 
    uint3 gtid : SV_GroupThreadID,
    [ MaxRecords(256)] NodeOutput<RasterRecord> Rasterize,
    [MaxRecordsSharedWith(Rasterize)] NodeOutput<ClusterRecord> OcclusionCull)
{
    
}

// Node: OcclusionCull (2nd pass)
[Shader("node")]
[NodeID("OcclusionCull")]
[NodeLaunch("coalescing")]
[NumThreads(64, 1, 1)]
void WG_OcclusionCull(
    [MaxRecords(256)] GroupNodeInputRecords<ClusterRecord> inRecs,
    uint3 gtid : SV_GroupThreadID,
    [MaxRecords(256)] NodeOutput<RasterRecord> Rasterize)
{
    
}

// Node: Rasterize (leaf)
[Shader("node")]
[NodeID("Rasterize")]
[NodeLaunch("coalescing")]
[NumThreads(64, 1, 1)]
void WG_Rasterize(
    [MaxRecords(256)] GroupNodeInputRecords<RasterRecord> inRecs,
    uint3 gtid : SV_GroupThreadID)
{
    uint count = inRecs.Count();
    uint i = gtid.x;

    if (i >= count) return;

    RasterRecord r = inRecs[i];

    // Append to a visible list buffer (stub)
    uint dst;
    //InterlockedAdd(gVisibleCount[0], 1, dst);
    //gVisibleList[dst] = uint2(r.perMeshInstanceBufferIndex, r.clusterIndex);
}
