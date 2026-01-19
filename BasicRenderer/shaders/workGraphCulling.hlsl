// Compile with DXC target: lib_6_8 (Shader Model 6.8)

// -------------------- Record types --------------------
struct ObjectCullRecord
{
    uint activeDrawIndex; // seed: one per instance/draw
};

struct TraverseRecord
{
    uint activeDrawIndex;
    uint nodeIndex;
    uint depth;
};

struct ClusterRecord
{
    uint activeDrawIndex;
    uint clusterIndex;
};

struct RasterRecord
{
    uint activeDrawIndex;
    uint clusterIndex;
};

// Node: ObjectCull (entry)
[Shader("node")]
[NodeID("ObjectCull")]
[NodeLaunch("coalescing")]
[NumThreads(64, 1, 1)]
void WG_ObjectCull(
    [MaxRecords(64)] GroupNodeInputRecords<ObjectCullRecord> inRecs,
    uint3 gtid : SV_GroupThreadID,
    [ MaxRecords(64)] NodeOutput<TraverseRecord> Traverse)
{
    uint count = inRecs.Count();
    uint i = gtid.x;

    bool active = (i < count);

    // Stub: everything survives culling
    uint outCount = active ? 1u : 0u;

    // call must be uniform; outCount may be non-uniform.
    ThreadNodeOutputRecords<TraverseRecord> outRecord = Traverse.GetThreadNodeOutputRecords(outCount);

    if (active)
    {
        ObjectCullRecord r = inRecs[i];
        outRecord[0].activeDrawIndex = r.activeDrawIndex;
        outRecord[0].nodeIndex = 0;
        outRecord[0].depth = 0;
    }

    outRecord.OutputComplete();
}

// Node: Traverse (recursive)
[Shader("node")]
[NodeID("Traverse")]
[NodeLaunch("coalescing")]
[NodeMaxRecursionDepth(32)]
[NumThreads(64, 1, 1)]
void WG_Traverse(
    [MaxRecords(256)] GroupNodeInputRecords<TraverseRecord> inRecs,
    uint3 gtid : SV_GroupThreadID,
    // output param name defaults the target NodeID if you don't use [NodeID(...)] on it.
    [ MaxRecords(256)] NodeOutput<TraverseRecord> Traverse,
    [MaxRecordsSharedWith(Traverse)] NodeOutput<ClusterRecord> ClusterCull)
{
    uint count = inRecs.Count();
    uint i = gtid.x;

    bool active = (i < count);
    TraverseRecord r;
    if (active) {
        r = inRecs[i];
    } else {
        r = (TraverseRecord) 0;
    }

    // Stub policy:
    // - expand 2 children until depth == 2
    // - then emit 1 leaf cluster
    bool shouldExpand = active && (r.depth < 2);

    uint childCount = shouldExpand ? 2u : 0u;
    uint clusterCount = (!shouldExpand && active) ? 1u : 0u;

    ThreadNodeOutputRecords<TraverseRecord> childOut = Traverse.GetThreadNodeOutputRecords(childCount);
    ThreadNodeOutputRecords<ClusterRecord>  leafOut  = ClusterCull.GetThreadNodeOutputRecords(clusterCount);

    if (shouldExpand)
    {
        // Stub: pretend each node has 2 children at indices (node*2+1, node*2+2)
        childOut[0].activeDrawIndex = r.activeDrawIndex;
        childOut[0].nodeIndex       = r.nodeIndex * 2 + 1;
        childOut[0].depth           = r.depth + 1;

        childOut[1].activeDrawIndex = r.activeDrawIndex;
        childOut[1].nodeIndex       = r.nodeIndex * 2 + 2;
        childOut[1].depth           = r.depth + 1;
    }
    else if (active)
    {
        leafOut[0].activeDrawIndex = r.activeDrawIndex;
        leafOut[0].clusterIndex    = r.nodeIndex; // stub mapping
    }

    childOut.OutputComplete();
    leafOut.OutputComplete();
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
    uint count = inRecs.Count();
    uint i = gtid.x;

    bool active = (i < count);
    ClusterRecord r;
    if (active) {
        r = inRecs[i];
    } else {
        r = (ClusterRecord) 0;
    }

        // Stub classification: even clusters -> visible, odd -> need second pass
    bool needsSecondPass = active && ((r.clusterIndex & 1u) != 0u);

    uint rasterCount = (active && !needsSecondPass) ? 1u : 0u;
    uint occCount = needsSecondPass ? 1u : 0u;

    ThreadNodeOutputRecords<RasterRecord> rasterOut = Rasterize.GetThreadNodeOutputRecords(rasterCount);
    ThreadNodeOutputRecords<ClusterRecord> occOut   = OcclusionCull.GetThreadNodeOutputRecords(occCount);

    if (rasterCount)
    {
        rasterOut[0].activeDrawIndex = r.activeDrawIndex;
        rasterOut[0].clusterIndex    = r.clusterIndex;
    }
    if (occCount)
    {
        occOut[0] = r;
    }

    rasterOut.OutputComplete();
    occOut.OutputComplete();
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
    uint count = inRecs.Count();
    uint i = gtid.x;

    bool active = (i < count);
    ClusterRecord r;
    if (active) {
        r = inRecs[i];
    } else {
        r = (ClusterRecord) 0;
    }

        // Stub: everything passes on second pass
    uint outCount = active ? 1u : 0u;

    ThreadNodeOutputRecords<RasterRecord> outRecord = Rasterize.GetThreadNodeOutputRecords(outCount);

    if (active)
    {
        outRecord[0].activeDrawIndex = r.activeDrawIndex;
        outRecord[0].clusterIndex    = r.clusterIndex;
    }

    outRecord.OutputComplete();
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
    //gVisibleList[dst] = uint2(r.activeDrawIndex, r.clusterIndex);
}
