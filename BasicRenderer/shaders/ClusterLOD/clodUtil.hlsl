#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/waveIntrinsicsHelpers.hlsli"
#include "PerPassRootConstants/clodRootConstants.h"
#include "include/indirectCommands.hlsli"

struct RasterBucketsHistogramIndirectCommand
{
    uint clusterCount;
    uint xDim;
    uint dispatchX, dispatchY, dispatchZ;
};

#define CLUSTER_HISTOGRAM_GROUP_SIZE 8

// Single-thread shader to create a command for histogram eval
[shader("compute")]
[numthreads(1, 1, 1)]
void CreateRasterBucketsHistogramCommandCSMain()
{
    RWStructuredBuffer<RasterBucketsHistogramIndirectCommand> outCommand = ResourceDescriptorHeap[CLOD_RASTER_BUCKET_HISTOGRAM_COMMAND_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> clusterCountBuffer = ResourceDescriptorHeap[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];

    // Given the cluster count, find dispatch dimensions that minimizes wasted threads
    uint clusterCount = clusterCountBuffer.Load(0);
    uint numBuckets = CLOD_NUM_RASTER_BUCKETS;
    uint totalItems = max(clusterCount, numBuckets);

    uint groupsNeeded = (totalItems + CLUSTER_HISTOGRAM_GROUP_SIZE - 1u) / CLUSTER_HISTOGRAM_GROUP_SIZE;

    const uint kMaxDim = 65535u;
    uint dispatchX = (uint) ceil(sqrt((float) groupsNeeded));
    if (dispatchX > kMaxDim)
    {
        dispatchX = kMaxDim;
    }

    uint dispatchY = (groupsNeeded + dispatchX - 1u) / dispatchX;
    if (dispatchY > kMaxDim)
    {
        dispatchY = kMaxDim;
    }

    outCommand[0].clusterCount = clusterCount;
    outCommand[0].xDim = dispatchX;
    outCommand[0].dispatchX = dispatchX;
    outCommand[0].dispatchY = dispatchY;
    outCommand[0].dispatchZ = 1;
}

// UintRootConstant0 = cluster count
// UintRootConstant1 = xDim
[shader("compute")]
[numthreads(CLUSTER_HISTOGRAM_GROUP_SIZE, 1, 1)]
void ClusterRasterBucketsHistogramCSMain(uint3 DTid : SV_DispatchThreadID)
{
    // Linearize the 2D dispatch thread ID
    uint linearizedID = DTid.x + DTid.y * UintRootConstant1;
    StructuredBuffer<uint> clusterCountBuffer = ResourceDescriptorHeap[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];
    uint clusterCount = clusterCountBuffer.Load(0);
    
    if (linearizedID >= clusterCount) {
        return;
    }
    
    StructuredBuffer<VisibleCluster> visibleClusters = ResourceDescriptorHeap[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];

    // TODO: Remove load chain
    uint instanceIndex = visibleClusters[linearizedID].instanceID;
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstance = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    uint perMeshIndex = perMeshInstance[instanceIndex].perMeshBufferIndex;
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    uint materialDataIndex = perMeshBuffer[perMeshIndex].materialDataIndex;
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    uint rasterBucketIndex = materialDataBuffer[materialDataIndex].rasterBucketIndex;

    // Group threads in the wave by matId
    uint4 mask = WaveMatch(rasterBucketIndex);

    if (IsWaveGroupLeader(mask))
    {
        uint groupSize = CountBits128(mask);
        RWStructuredBuffer<uint> histogramBuffer = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX];
        InterlockedAdd(histogramBuffer[rasterBucketIndex], groupSize);
    }
}

// --- Prefix sum for raster bucket histogram ---------------------------------

static const uint CLOD_BUCKET_BLOCK_SIZE = 1024; // must be power-of-two
static const uint CLOD_BUCKET_SCAN_THREADS = 256;

groupshared uint s_bucketData[CLOD_BUCKET_BLOCK_SIZE];
groupshared uint s_bucketScan[CLOD_BUCKET_BLOCK_SIZE];
groupshared uint s_bucketBlockSum;

void CLODExclusiveScanBlock(uint Nblock, uint threadId)
{
    if (Nblock == 0)
    {
        return;
    }

    uint P = 1;
    while (P < Nblock)
    {
        P <<= 1;
    }

    for (uint i = threadId; i < Nblock; i += CLOD_BUCKET_SCAN_THREADS)
    {
        s_bucketScan[i] = s_bucketData[i];
    }
    for (uint i = threadId + Nblock; i < P; i += CLOD_BUCKET_SCAN_THREADS)
    {
        s_bucketScan[i] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 1; stride < P; stride <<= 1)
    {
        uint idx = (threadId + 1) * (stride << 1) - 1;
        if (idx < P)
        {
            s_bucketScan[idx] += s_bucketScan[idx - stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (threadId == 0)
    {
        s_bucketScan[P - 1] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = (P >> 1); stride >= 1; stride >>= 1)
    {
        uint idx = (threadId + 1) * (stride << 1) - 1;
        if (idx < P)
        {
            uint t = s_bucketScan[idx - stride];
            s_bucketScan[idx - stride] = s_bucketScan[idx];
            s_bucketScan[idx] += t;
        }
        GroupMemoryBarrierWithGroupSync();

        if (stride == 1)
            break;
    }
}

// Root constants:
// UintRootConstant0 = NumBuckets
[numthreads(CLOD_BUCKET_SCAN_THREADS, 1, 1)]
void RasterBucketsBlockScanCS(uint3 groupThreadId : SV_GroupThreadID,
                              uint3 groupId : SV_GroupID)
{
    StructuredBuffer<uint> histogram = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> offsets = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> blockSums = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX];

    uint N = UintRootConstant0;
    uint blockId = groupId.x;
    uint localTid = groupThreadId.x;
    uint base = blockId * CLOD_BUCKET_BLOCK_SIZE;

    for (uint i = localTid; i < CLOD_BUCKET_BLOCK_SIZE; i += CLOD_BUCKET_SCAN_THREADS)
    {
        uint globalIdx = base + i;
        s_bucketData[i] = (globalIdx < N) ? histogram[globalIdx] : 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint Nblock = 0;
    if (N > base)
        Nblock = min(CLOD_BUCKET_BLOCK_SIZE, N - base);

    CLODExclusiveScanBlock(Nblock, localTid);

    for (uint i = localTid; i < Nblock; i += CLOD_BUCKET_SCAN_THREADS)
    {
        offsets[base + i] = s_bucketScan[i];
    }

    uint localSum = 0;
    for (uint i = localTid; i < Nblock; i += CLOD_BUCKET_SCAN_THREADS)
    {
        localSum += s_bucketData[i];
    }

    if (localTid == 0)
        s_bucketBlockSum = 0;
    GroupMemoryBarrierWithGroupSync();

    InterlockedAdd(s_bucketBlockSum, localSum);
    GroupMemoryBarrierWithGroupSync();

    if (localTid == 0)
    {
        uint numBlocks = (N + CLOD_BUCKET_BLOCK_SIZE - 1) / CLOD_BUCKET_BLOCK_SIZE;
        if (blockId < numBlocks)
        {
            blockSums[blockId] = s_bucketBlockSum;
        }
    }
}

// Root constants:
// UintRootConstant0 = NumBuckets
// UintRootConstant1 = NumBlocks
[numthreads(CLOD_BUCKET_SCAN_THREADS, 1, 1)]
void RasterBucketsBlockOffsetsCS(uint3 groupThreadId : SV_GroupThreadID,
                                 uint3 groupId : SV_GroupID)
{
    if (groupId.x != 0 || groupId.y != 0 || groupId.z != 0)
        return;

    RWStructuredBuffer<uint> offsets = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> blockSums = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> scannedBlockSums = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_SCANNED_BLOCK_SUMS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> totalOut = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_TOTAL_COUNT_DESCRIPTOR_INDEX];

    uint N = UintRootConstant0;
    uint B = UintRootConstant1;

    uint localTid = groupThreadId.x;

    for (uint i = localTid; i < B; i += CLOD_BUCKET_SCAN_THREADS)
    {
        s_bucketData[i] = blockSums[i];
    }
    GroupMemoryBarrierWithGroupSync();

    CLODExclusiveScanBlock(B, localTid);

    for (uint i = localTid; i < B; i += CLOD_BUCKET_SCAN_THREADS)
    {
        scannedBlockSums[i] = s_bucketScan[i];
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint elem = localTid; elem < N; elem += CLOD_BUCKET_SCAN_THREADS)
    {
        uint blockId = elem / CLOD_BUCKET_BLOCK_SIZE;
        uint prefix = s_bucketScan[blockId];
        offsets[elem] += prefix;
    }

    if (localTid == 0 && B > 0)
    {
        uint total = s_bucketScan[B - 1] + s_bucketData[B - 1];
        totalOut[0] = total;
    }
}

uint GetRasterBucketIndexFromCluster(VisibleCluster cluster)
{
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstance = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];

    PerMeshInstanceBuffer instanceData = perMeshInstance[cluster.instanceID];
    PerMeshBuffer meshBuffer = perMeshBuffer[instanceData.perMeshBufferIndex];
    MaterialInfo materialInfo = materialDataBuffer[meshBuffer.materialDataIndex];

    return materialInfo.rasterBucketIndex;
}

static const uint CLOD_COMPACTION_GROUP_SIZE = CLUSTER_HISTOGRAM_GROUP_SIZE;

[shader("compute")]
[numthreads(CLOD_COMPACTION_GROUP_SIZE, 1, 1)]
void CompactClustersAndBuildIndirectArgsCS(uint3 dtid : SV_DispatchThreadID)
{
    uint xDimThreads = UintRootConstant1 * CLOD_COMPACTION_GROUP_SIZE;
    uint linearizedID = dtid.x + dtid.y * xDimThreads;

    uint clusterCount = UintRootConstant0;

    StructuredBuffer<uint> histogram = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX];
    uint numBuckets = CLOD_NUM_RASTER_BUCKETS;

    if (linearizedID < clusterCount)
    {
        StructuredBuffer<VisibleCluster> visibleClusters = ResourceDescriptorHeap[CLOD_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
        RWStructuredBuffer<VisibleCluster> compactedClusters = ResourceDescriptorHeap[CLOD_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX];
        StructuredBuffer<uint> offsets = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX];
        RWStructuredBuffer<uint> writeCursor = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_WRITE_CURSOR_DESCRIPTOR_INDEX];

        VisibleCluster cluster = visibleClusters[linearizedID];
        uint bucketIndex = GetRasterBucketIndexFromCluster(cluster);

        uint localOffset = 0;
        InterlockedAdd(writeCursor[bucketIndex], 1, localOffset);

        uint dst = offsets[bucketIndex] + localOffset;
        compactedClusters[dst] = cluster;
    }

    if (linearizedID < numBuckets)
    {
        StructuredBuffer<uint> offsets = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX];
        RWStructuredBuffer<DispatchMeshIndirectCommand> outArgs = ResourceDescriptorHeap[CLOD_RASTER_BUCKETS_INDIRECT_ARGS_DESCRIPTOR_INDEX];

        uint count = histogram[linearizedID];

        DispatchMeshIndirectCommand cmd = (DispatchMeshIndirectCommand)0;
        if (count > 0)
        {
            cmd.perObjectBufferIndex = offsets[linearizedID];
            cmd.perMeshBufferIndex = 0;
            cmd.perMeshInstanceBufferIndex = 0;

            cmd.dispatchMeshX = count;
            cmd.dispatchMeshY = 1;
            cmd.dispatchMeshZ = 1;
        }

        outArgs[linearizedID] = cmd;
    }
}