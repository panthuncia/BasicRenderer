#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/vertexFlags.hlsli"
#include "include/visibleClusterPacking.hlsli"
#include "include/clodStructs.hlsli"
#include "PerPassRootConstants/clodReyesRootConstants.h"

static const uint REYES_CLASSIFY_GROUP_SIZE = 64u;
static const uint REYES_OUTCOME_FLAG_SKINNED = 1u << 0;
static const uint REYES_OUTCOME_FLAG_DISPLACEMENT_ENABLED = 1u << 1;
static const uint REYES_BARYCENTRIC_COORD_MAX = 0xFFFFu;

bool TryAppendBudgetedEntry(RWStructuredBuffer<uint> counterBuffer, uint capacity, out uint dst)
{
    InterlockedAdd(counterBuffer[0], 1u, dst);
    if (dst < capacity)
    {
        return true;
    }

    InterlockedAdd(counterBuffer[0], 0xFFFFFFFFu);
    dst = 0u;
    return false;
}

uint GetReyesClassifyVisibleClusterReadIndex(uint linearizedID)
{
    uint readBase = 0u;
    if (CLOD_REYES_CLASSIFY_VISIBLE_CLUSTERS_READ_BASE_COUNTER_DESCRIPTOR_INDEX != 0xFFFFFFFFu)
    {
        StructuredBuffer<uint> readBaseCounter = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_VISIBLE_CLUSTERS_READ_BASE_COUNTER_DESCRIPTOR_INDEX];
        readBase = readBaseCounter.Load(0);
    }

    return readBase + linearizedID;
}

void MarkVisibleClusterOwnedByReyes(uint visibleClusterReadIndex)
{
    RWStructuredBuffer<uint> ownershipWords = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_OWNERSHIP_BITSET_DESCRIPTOR_INDEX];
    const uint ownershipWordIndex = visibleClusterReadIndex >> 5u;
    const uint ownershipBitMask = 1u << (visibleClusterReadIndex & 31u);
    InterlockedOr(ownershipWords[ownershipWordIndex], ownershipBitMask);
}

[shader("compute")]
[numthreads(REYES_CLASSIFY_GROUP_SIZE, 1, 1)]
void ReyesClassifyCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<uint> visibleClusterCounter = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];
    const uint clusterCount = visibleClusterCounter[0];
    const uint clusterLinearIndex = dispatchThreadId.x;
    if (clusterLinearIndex >= clusterCount)
    {
        return;
    }

    const uint clusterIndex = GetReyesClassifyVisibleClusterReadIndex(clusterLinearIndex);

    ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
    const uint3 packedCluster = CLodLoadVisibleClusterPacked(visibleClusters, clusterIndex);
    const uint instanceID = CLodVisibleClusterInstanceID(packedCluster);

    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstances = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    StructuredBuffer<PerMeshBuffer> perMeshes = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];

    const PerMeshInstanceBuffer meshInstance = perMeshInstances[instanceID];
    const PerMeshBuffer perMesh = perMeshes[meshInstance.perMeshBufferIndex];
    const uint materialIndex = perMesh.materialDataIndex;
    const MaterialInfo materialInfo = materialDataBuffer[materialIndex];

    const bool displacementEnabled = materialInfo.geometricDisplacementEnabled != 0u;
    const float displacementSpan = max(0.0f, materialInfo.geometricDisplacementMax - materialInfo.geometricDisplacementMin);
    const bool skinned = (perMesh.vertexFlags & VERTEX_SKINNED) != 0u;
    const uint commonFlags = (skinned ? REYES_OUTCOME_FLAG_SKINNED : 0u) | (displacementEnabled ? REYES_OUTCOME_FLAG_DISPLACEMENT_ENABLED : 0u);

    if (clusterLinearIndex == 0u)
    {
        RWStructuredBuffer<CLodReyesTelemetry> telemetryBuffer = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_TELEMETRY_DESCRIPTOR_INDEX];
        telemetryBuffer[0].visibleClusterInputCount = clusterCount;
        telemetryBuffer[0].phaseIndex = CLOD_REYES_CLASSIFY_PHASE_INDEX;
    }

    if (!displacementEnabled || displacementSpan <= 1e-5f)
    {
        RWStructuredBuffer<uint> fullCounter = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_FULL_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];
        RWStructuredBuffer<CLodReyesFullClusterOutput> fullClusters = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_FULL_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
        RWStructuredBuffer<CLodReyesTelemetry> telemetryBuffer = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_TELEMETRY_DESCRIPTOR_INDEX];

        uint dst = 0u;
        if (!TryAppendBudgetedEntry(fullCounter, CLOD_REYES_CLASSIFY_FULL_CLUSTERS_CAPACITY, dst))
        {
            return;
        }

        fullClusters[dst].visibleClusterIndex = clusterIndex;
        fullClusters[dst].instanceID = instanceID;
        fullClusters[dst].materialIndex = materialIndex;
        fullClusters[dst].flags = commonFlags;
        InterlockedAdd(telemetryBuffer[0].fullClusterOutputCount, 1u);
        return;
    }

    RWStructuredBuffer<uint> ownedClusterCounter = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_OWNED_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesOwnedClusterEntry> ownedClusters = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_OWNED_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];

    uint dst = 0u;
    if (!TryAppendBudgetedEntry(ownedClusterCounter, CLOD_REYES_CLASSIFY_OWNED_CLUSTERS_CAPACITY, dst))
    {
        return;
    }

    MarkVisibleClusterOwnedByReyes(clusterIndex);
    ownedClusters[dst].visibleClusterIndex = clusterIndex;
    ownedClusters[dst].instanceID = instanceID;
    ownedClusters[dst].materialIndex = materialIndex;
    ownedClusters[dst].flags = commonFlags;
}