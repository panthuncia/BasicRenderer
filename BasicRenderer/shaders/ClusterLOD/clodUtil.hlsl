#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/waveIntrinsicsHelpers.hlsli"
#include "PerPassRootConstants/clodRootConstants.h"

struct RasterBucketsHistogramIndirectCommand
{
    uint clusterCount;
    uint xDim;
    uint dispatchX, dispatchY, dispatchZ;
};

#define CLUSTER_HISTOGRAM_GROUP_SIZE 8

// Single-thread shader to create a command for histogram eval
[numthreads(1, 1, 1)]
void CreateRasterBucketsHistogramCommandCSMain()
{
    RWStructuredBuffer<RasterBucketsHistogramIndirectCommand> outCommand = ResourceDescriptorHeap[CLOD_RASTER_BUCKET_HISTOGRAM_COMMAND_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> clusterCountBuffer = ResourceDescriptorHeap[CLOD_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];

    // Given the cluster count, find dispatch dimensions that minimizes wasted threads
    uint clusterCount = clusterCountBuffer.Load(0);
    
    uint groupsNeeded = (clusterCount + CLUSTER_HISTOGRAM_GROUP_SIZE - 1u) / CLUSTER_HISTOGRAM_GROUP_SIZE;
    
    const uint kMaxDim = 65535u;
    // Try to make dispatchX and dispatchY as close to each other as possible
    uint dispatchX = (uint) ceil(sqrt((float) groupsNeeded));
    if (dispatchX > kMaxDim) {
        dispatchX = kMaxDim;
    }

    uint dispatchY = (groupsNeeded + dispatchX - 1u) / dispatchX;
    if (dispatchY > kMaxDim) {
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
[numthreads(CLUSTER_HISTOGRAM_GROUP_SIZE, CLUSTER_HISTOGRAM_GROUP_SIZE, 1)]
void ClusterRasterBucketsHistogramCSMain(uint3 DTid : SV_DispatchThreadID)
{
    // Linearize the 2D dispatch thread ID
    uint linearizedID = DTid.x + DTid.y * CLUSTER_HISTOGRAM_GROUP_SIZE;
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
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::MaterialDataBuffer)];
    uint rasterBucketIndex = materialDataBuffer[materialDataIndex].rasterBucketIndex;

    // Group threads in the wave by matId
    uint4 mask = WaveMatch(rasterBucketIndex);

    if (IsWaveGroupLeader(mask))
    {
        uint groupSize = CountBits128(mask);
        RWStructuredBuffer<uint> histogramBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::RasterBucketsHistogramBuffer)];
        InterlockedAdd(histogramBuffer[rasterBucketIndex], groupSize);
    }
}