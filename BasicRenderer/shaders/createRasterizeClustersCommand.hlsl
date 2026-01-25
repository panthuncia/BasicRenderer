#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "Common/defines.h"

struct RasterizeClustersCommand
{
    uint clusterCount;
    uint xDim;
    uint dispatchX, dispatchY, dispatchZ;
};

// Single-thread shader to create a command for rasterizing clusters
[numthreads(1, 1, 1)]
void CreateRasterizeClustersCommand()
{
    StructuredBuffer<RasterizeClustersCommand> outCommand = ResourceDescriptorHeap(ResourceDescriptorIndex(Builtin::RasterizeClustersIndirectCommand));
    StructuredBuffer<uint> clusterCountBuffer = ResourceDescriptorHeap(ResourceDescriptorIndex(Builtin::VisibleClusterCounter));

    // Given the cluster count, find dispatch dimensions that minimizes wasted threads
    uint clusterCount = clusterCountBuffer.Load(0);
    
    uint groupsNeeded = (clusterCount + MS_MESHLET_SIZE - 1u) / MS_MESHLET_SIZE;
    
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