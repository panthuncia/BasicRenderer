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
static const uint REYES_TELEMETRY_UNSET_INDEX = 0xFFFFFFFFu;

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
    RWStructuredBuffer<CLodReyesTelemetry> telemetryBuffer = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_TELEMETRY_DESCRIPTOR_INDEX];
    if (visibleClusterReadIndex >= CLOD_REYES_CLASSIFY_VISIBLE_CLUSTERS_CAPACITY)
    {
        InterlockedAdd(telemetryBuffer[0].ownershipBitSetOutOfRangeCount, 1u);
        return;
    }

    RWStructuredBuffer<uint> ownershipWords = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_OWNERSHIP_BITSET_DESCRIPTOR_INDEX];
    const uint ownershipWordIndex = visibleClusterReadIndex >> 5u;
    const uint ownershipBitMask = 1u << (visibleClusterReadIndex & 31u);
    uint previousWord = 0u;
    InterlockedOr(ownershipWords[ownershipWordIndex], ownershipBitMask, previousWord);
    if ((previousWord & ownershipBitMask) == 0u)
    {
        InterlockedAdd(telemetryBuffer[0].ownershipBitSetUniqueCount, 1u);
    }
    else
    {
        InterlockedAdd(telemetryBuffer[0].ownershipBitSetDuplicateCount, 1u);
    }
    InterlockedMax(telemetryBuffer[0].ownershipBitSetMaxIndex, visibleClusterReadIndex);
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
        telemetryBuffer[0].firstOwnedVisibleClusterIndex = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstOwnedInstanceID = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstOwnedMaterialIndex = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstHistogramReadVisibleClusterIndex = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstHistogramReadInstanceID = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstHistogramReadMaterialIndex = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].histogramInvocationCount = 0u;
        telemetryBuffer[0].histogramObservedClusterCount = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].histogramReadOwnedClusterMatchCount = 0u;
        telemetryBuffer[0].histogramOwnedClusterBitValue = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstCompactionReadVisibleClusterIndex = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstCompactionReadInstanceID = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstCompactionReadMaterialIndex = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].compactionInvocationCount = 0u;
        telemetryBuffer[0].compactionObservedClusterCount = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].compactionReadOwnedClusterMatchCount = 0u;
        telemetryBuffer[0].compactionOwnedClusterBitValue = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstHistogramSkippedVisibleClusterIndex = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstHistogramSkippedInstanceID = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstHistogramSkippedMaterialIndex = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstCompactionSkippedVisibleClusterIndex = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstCompactionSkippedInstanceID = REYES_TELEMETRY_UNSET_INDEX;
        telemetryBuffer[0].firstCompactionSkippedMaterialIndex = REYES_TELEMETRY_UNSET_INDEX;
    }

    if (!displacementEnabled || displacementSpan <= 1e-5f)
    {
        RWStructuredBuffer<uint> fullCounter = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_FULL_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];
        RWStructuredBuffer<CLodReyesFullClusterOutput> fullClusters = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_FULL_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
        RWStructuredBuffer<CLodReyesTelemetry> telemetryBuffer = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_TELEMETRY_DESCRIPTOR_INDEX];

        uint dst = 0u;
        InterlockedAdd(fullCounter[0], 1u, dst);
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
    InterlockedAdd(ownedClusterCounter[0], 1u, dst);
    MarkVisibleClusterOwnedByReyes(clusterIndex);
    RWStructuredBuffer<CLodReyesTelemetry> telemetryBuffer = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_TELEMETRY_DESCRIPTOR_INDEX];
    InterlockedAdd(telemetryBuffer[0].ownedClusterOutputCount, 1u);
    uint previousFirstOwned = REYES_TELEMETRY_UNSET_INDEX;
    InterlockedCompareExchange(
        telemetryBuffer[0].firstOwnedVisibleClusterIndex,
        REYES_TELEMETRY_UNSET_INDEX,
        clusterIndex,
        previousFirstOwned);
    if (previousFirstOwned == REYES_TELEMETRY_UNSET_INDEX)
    {
        telemetryBuffer[0].firstOwnedInstanceID = instanceID;
        telemetryBuffer[0].firstOwnedMaterialIndex = materialIndex;
    }
    ownedClusters[dst].visibleClusterIndex = clusterIndex;
    ownedClusters[dst].instanceID = instanceID;
    ownedClusters[dst].materialIndex = materialIndex;
    ownedClusters[dst].flags = commonFlags;
}