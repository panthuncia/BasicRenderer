#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"

void main(uint3 DTid : SV_DispatchThreadID)
{
    RWStructuredBuffer<uint> histogramBuffer = ResourceDescriptorHeap(ResourceDescriptorIndex(Builtin::CLod::RasterBucketsHistogram));
    StructuredBuffer<VisibleCluster> visibleClusters = ResourceDescriptorHeap(ResourceDescriptorIndex(Builtin::VisibleClustersBuffer));
    StructuredBuffer<uint> clusterCountBuffer = ResourceDescriptorHeap(ResourceDescriptorIndex(Builtin::VisibleClusterCounter));
    uint clusterCount = clusterCountBuffer.Load(0);
    uint index = DTid.x;
    histogramBuffer[index] = 0;
}