#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/vertexFlags.hlsli"
#include "include/visibleClusterPacking.hlsli"
#include "include/clodPageAccess.hlsli"
#include "include/clodStructs.hlsli"
#include "include/clodResolveCommon.hlsli"
#include "PerPassRootConstants/clodReyesRootConstants.h"

static const uint REYES_CLASSIFY_GROUP_SIZE = 64u;
static const uint REYES_OUTCOME_FLAG_SKINNED = 1u << 0;
static const uint REYES_OUTCOME_FLAG_DISPLACEMENT_ENABLED = 1u << 1;
static const uint REYES_BARYCENTRIC_COORD_MAX = 0xFFFFu;
static const float REYES_SCREEN_SCALE_REFERENCE = 1080.0f;

uint GetReyesClassifyVisibleClusterReadIndex(uint linearizedID)
{
    uint readBase = 0u;
    if (CLOD_REYES_CLASSIFY_VISIBLE_CLUSTERS_READ_BASE_COUNTER_DESCRIPTOR_INDEX != 0u)
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

float MaxAxisScale_RowVector(float4x4 M)
{
    float3 row0 = M[0].xyz;
    float3 row1 = M[1].xyz;
    float3 row2 = M[2].xyz;
    return max(length(row0), max(length(row1), length(row2)));
}

float ComputeReyesTessFactor(
    float3 centerObjectSpace,
    float radiusObjectSpace,
    uint triangleCount,
    float displacementSpan,
    float4x4 modelMatrix,
    float skinnedBoundsScale,
    CullingCameraInfo camera)
{
    const float uniformScale = MaxAxisScale_RowVector(modelMatrix) * skinnedBoundsScale;
    const float3 worldCenter = mul(float4(centerObjectSpace, 1.0f), modelMatrix).xyz;
    const float worldRadius = radiusObjectSpace * uniformScale;
    const float distanceToCamera = length(worldCenter - camera.positionWorldSpace.xyz);
    const float denom = max(distanceToCamera - worldRadius, camera.zNear);
    const float projectedRadius = (worldRadius * camera.projY * REYES_SCREEN_SCALE_REFERENCE) / max(denom, 1e-4f);
    const float triangleFactor = sqrt(max(1.0f, (float)triangleCount));
    const float displacementFactor = max(1.0f, displacementSpan * 64.0f);
    return max(1.0f, projectedRadius * triangleFactor * displacementFactor * 0.03125f);
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
    const uint localMeshletIndex = CLodVisibleClusterLocalMeshletIndex(packedCluster);
    const uint viewID = CLodVisibleClusterViewID(packedCluster);
    const uint pageSlabDescriptorIndex = CLodVisibleClusterPageSlabDescriptorIndex(packedCluster);
    const uint pageSlabByteOffset = CLodVisibleClusterPageSlabByteOffset(packedCluster);

    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstances = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    StructuredBuffer<PerMeshBuffer> perMeshes = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjects = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    StructuredBuffer<CullingCameraInfo> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];

    const PerMeshInstanceBuffer meshInstance = perMeshInstances[instanceID];
    const PerMeshBuffer perMesh = perMeshes[meshInstance.perMeshBufferIndex];
    const uint materialIndex = perMesh.materialDataIndex;
    const MaterialInfo materialInfo = materialDataBuffer[materialIndex];
    const PerObjectBuffer objectData = perObjects[meshInstance.perObjectBufferIndex];
    const CullingCameraInfo camera = cameras[viewID];

    CLodPageHeader hdr = LoadPageHeader(pageSlabDescriptorIndex, pageSlabByteOffset);
    CLodMeshletDescriptor meshletDesc = LoadMeshletDescriptor(pageSlabDescriptorIndex, pageSlabByteOffset, hdr.descriptorOffset, localMeshletIndex);

    const bool displacementEnabled = materialInfo.geometricDisplacementEnabled != 0u;
    const float displacementSpan = max(0.0f, materialInfo.geometricDisplacementMax - materialInfo.geometricDisplacementMin);
    const bool skinned = (perMesh.vertexFlags & VERTEX_SKINNED) != 0u;
    const uint commonFlags = (skinned ? REYES_OUTCOME_FLAG_SKINNED : 0u) | (displacementEnabled ? REYES_OUTCOME_FLAG_DISPLACEMENT_ENABLED : 0u);

    if (clusterIndex == 0u)
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
    ownedClusters[dst].visibleClusterIndex = clusterIndex;
    ownedClusters[dst].instanceID = instanceID;
    ownedClusters[dst].materialIndex = materialIndex;
    ownedClusters[dst].flags = commonFlags;
}