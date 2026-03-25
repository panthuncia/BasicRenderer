#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/visibleClusterPacking.hlsli"
#include "include/clodPageAccess.hlsli"
#include "include/clodStructs.hlsli"
#include "PerPassRootConstants/clodReyesSeedRootConstants.h"

static const uint REYES_SEED_GROUP_SIZE = 64u;
static const uint REYES_BARYCENTRIC_COORD_MAX = 0xFFFFu;

uint EncodeReyesSeedBarycentrics(float3 barycentrics)
{
    uint u = min(REYES_BARYCENTRIC_COORD_MAX, (uint)round(saturate(barycentrics.y) * REYES_BARYCENTRIC_COORD_MAX));
    uint v = min(REYES_BARYCENTRIC_COORD_MAX, (uint)round(saturate(barycentrics.z) * REYES_BARYCENTRIC_COORD_MAX));
    return u | (v << 16u);
}

[shader("compute")]
[numthreads(REYES_SEED_GROUP_SIZE, 1, 1)]
void ReyesSeedPatchesCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<uint> ownedClusterCounter = ResourceDescriptorHeap[CLOD_REYES_SEED_OWNED_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];
    const uint ownedClusterCount = ownedClusterCounter[0];
    const uint ownedClusterIndex = dispatchThreadId.x;
    if (ownedClusterIndex >= ownedClusterCount)
    {
        return;
    }

    StructuredBuffer<CLodReyesOwnedClusterEntry> ownedClusters = ResourceDescriptorHeap[CLOD_REYES_SEED_OWNED_CLUSTERS_DESCRIPTOR_INDEX];
    ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[CLOD_REYES_SEED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesSplitQueueEntry> splitQueue = ResourceDescriptorHeap[CLOD_REYES_SEED_OUTPUT_SPLIT_QUEUE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> splitQueueCounter = ResourceDescriptorHeap[CLOD_REYES_SEED_OUTPUT_SPLIT_QUEUE_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> splitQueueOverflowCounter = ResourceDescriptorHeap[CLOD_REYES_SEED_OUTPUT_SPLIT_QUEUE_OVERFLOW_DESCRIPTOR_INDEX];

    const CLodReyesOwnedClusterEntry ownedCluster = ownedClusters[ownedClusterIndex];
    const uint3 packedCluster = CLodLoadVisibleClusterPacked(visibleClusters, ownedCluster.visibleClusterIndex);
    const uint localMeshletIndex = CLodVisibleClusterLocalMeshletIndex(packedCluster);
    const uint viewID = CLodVisibleClusterViewID(packedCluster);
    const uint pageSlabDescriptorIndex = CLodVisibleClusterPageSlabDescriptorIndex(packedCluster);
    const uint pageSlabByteOffset = CLodVisibleClusterPageSlabByteOffset(packedCluster);

    const CLodPageHeader hdr = LoadPageHeader(pageSlabDescriptorIndex, pageSlabByteOffset);
    const CLodMeshletDescriptor meshletDesc = LoadMeshletDescriptor(pageSlabDescriptorIndex, pageSlabByteOffset, hdr.descriptorOffset, localMeshletIndex);
    const uint triangleCount = CLodDescTriangleCount(meshletDesc);
    if (triangleCount == 0u)
    {
        return;
    }

    const uint queueCapacity = CLOD_REYES_SEED_QUEUE_CAPACITY;
    uint outputBaseIndex = 0u;
    InterlockedAdd(splitQueueCounter[0], triangleCount, outputBaseIndex);
    if (outputBaseIndex >= queueCapacity)
    {
        InterlockedAdd(splitQueueOverflowCounter[0], triangleCount);
        return;
    }

    const uint maxWritableEntries = min(triangleCount, queueCapacity - outputBaseIndex);
    [loop]
    for (uint triangleIndex = 0u; triangleIndex < maxWritableEntries; ++triangleIndex)
    {
        CLodReyesSplitQueueEntry splitEntry;
        splitEntry.visibleClusterIndex = ownedCluster.visibleClusterIndex;
        splitEntry.instanceID = ownedCluster.instanceID;
        splitEntry.localMeshletIndex = localMeshletIndex;
        splitEntry.materialIndex = ownedCluster.materialIndex;
        splitEntry.viewID = viewID;
        splitEntry.splitLevel = 0u;
        splitEntry.quantizedTessFactor = 0u;
        splitEntry.flags = ownedCluster.flags;
        splitEntry.sourcePrimitiveAndSplitConfig = triangleIndex & 0xFFFFu;
        splitEntry.domainVertex0Encoded = EncodeReyesSeedBarycentrics(float3(1.0f, 0.0f, 0.0f));
        splitEntry.domainVertex1Encoded = EncodeReyesSeedBarycentrics(float3(0.0f, 1.0f, 0.0f));
        splitEntry.domainVertex2Encoded = EncodeReyesSeedBarycentrics(float3(0.0f, 0.0f, 1.0f));
        splitQueue[outputBaseIndex + triangleIndex] = splitEntry;
    }

    if (maxWritableEntries < triangleCount)
    {
        InterlockedAdd(splitQueueOverflowCounter[0], triangleCount - maxWritableEntries);
    }
}