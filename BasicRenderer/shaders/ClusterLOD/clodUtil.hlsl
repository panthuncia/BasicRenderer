#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"

struct RasterBucketsHistogramIndirectCommand
{
    uint clusterCount;
    uint xDim;
    uint dispatchX, dispatchY, dispatchZ;
};

#define CLUSTER_HISTOGRAM_GROUP_SIZE 64

// Single-thread shader to create a command for rasterizing clusters
[numthreads(1, 1, 1)]
void CreateRasterizeClustersCommandCSMain()
{
    StructuredBuffer<RasterBucketsHistogramIndirectCommand> outCommand = ResourceDescriptorHeap(ResourceDescriptorIndex(Builtin::CLod::RasterBucketsHistogramIndirectCommand));
    StructuredBuffer<uint> clusterCountBuffer = ResourceDescriptorHeap(ResourceDescriptorIndex(Builtin::VisibleClusterCounter));

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

    if (linearizedID >= UintRootConstant0) { // cluster count
        return;
    }

    RWStructuredBuffer<uint> histogramBuffer = ResourceDescriptorHeap(ResourceDescriptorIndex(Builtin::CLod::RasterBucketsHistogramBuffer));
    StructuredBuffer<VisibleCluster> visibleClusters = ResourceDescriptorHeap(ResourceDescriptorIndex(Builtin::VisibleClustersBuffer));
    StructuredBuffer<uint> clusterCountBuffer = ResourceDescriptorHeap(ResourceDescriptorIndex(Builtin::VisibleClusterCounter));
    uint clusterCount = clusterCountBuffer.Load(0);
    uint index = DTid.x;
    histogramBuffer[index] = 0;
}