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
static const uint REYES_SPLIT_CONFIG_UNIFORM4 = 0u;
static const uint REYES_SPLIT_CONFIG_EDGE01 = 1u;
static const uint REYES_SPLIT_CONFIG_EDGE12 = 2u;
static const uint REYES_SPLIT_CONFIG_EDGE20 = 3u;
static const float REYES_IMMEDIATE_DICE_TESS_THRESHOLD = 8.0f;
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

uint EncodeReyesBarycentrics(float3 barycentrics)
{
    uint u = min(REYES_BARYCENTRIC_COORD_MAX, (uint)round(saturate(barycentrics.y) * REYES_BARYCENTRIC_COORD_MAX));
    uint v = min(REYES_BARYCENTRIC_COORD_MAX, (uint)round(saturate(barycentrics.z) * REYES_BARYCENTRIC_COORD_MAX));
    return u | (v << 16u);
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

float3 ComputeReyesEdgeTessFactors(float3 worldPosition0, float3 worldPosition1, float3 worldPosition2, CullingCameraInfo camera)
{
    const float distance01 = max(camera.zNear, min(length(worldPosition0 - camera.positionWorldSpace.xyz), length(worldPosition1 - camera.positionWorldSpace.xyz)));
    const float distance12 = max(camera.zNear, min(length(worldPosition1 - camera.positionWorldSpace.xyz), length(worldPosition2 - camera.positionWorldSpace.xyz)));
    const float distance20 = max(camera.zNear, min(length(worldPosition2 - camera.positionWorldSpace.xyz), length(worldPosition0 - camera.positionWorldSpace.xyz)));

    const float edge01 = length(worldPosition0 - worldPosition1);
    const float edge12 = length(worldPosition1 - worldPosition2);
    const float edge20 = length(worldPosition2 - worldPosition0);

    const float scale = camera.projY * REYES_SCREEN_SCALE_REFERENCE * 0.03125f;
    return max(float3(1.0f, 1.0f, 1.0f), float3(edge01 / distance01, edge12 / distance12, edge20 / distance20) * scale);
}

uint ChooseReyesSplitConfig(float3 edgeFactors)
{
    const float maxFactor = max(edgeFactors.x, max(edgeFactors.y, edgeFactors.z));
    const float minFactor = min(edgeFactors.x, min(edgeFactors.y, edgeFactors.z));

    if (maxFactor <= REYES_IMMEDIATE_DICE_TESS_THRESHOLD)
    {
        return REYES_SPLIT_CONFIG_UNIFORM4;
    }

    if (maxFactor >= minFactor * 1.5f)
    {
        if (maxFactor == edgeFactors.x)
        {
            return REYES_SPLIT_CONFIG_EDGE01;
        }
        if (maxFactor == edgeFactors.y)
        {
            return REYES_SPLIT_CONFIG_EDGE12;
        }
        return REYES_SPLIT_CONFIG_EDGE20;
    }

    return REYES_SPLIT_CONFIG_UNIFORM4;
}

uint SelectReyesSeedTriangle(
    uint clusterIndex,
    PerObjectBuffer objectData,
    CullingCameraInfo camera,
    out float3 bestEdgeFactors)
{
    MeshletResolveData md = LoadMeshletResolveData_Wave(clusterIndex);
    uint bestTriangleIndex = 0u;
    float bestTriangleFactor = 0.0f;
    bestEdgeFactors = float3(1.0f, 1.0f, 1.0f);

    [loop]
    for (uint triangleIndex = 0u; triangleIndex < md.triangleCount; ++triangleIndex)
    {
        uint3 triIdx = DecodeTriangleCompact(triangleIndex, md);
        const float3 posOS0 = DecodeCompressedPosition(triIdx.x, md);
        const float3 posOS1 = DecodeCompressedPosition(triIdx.y, md);
        const float3 posOS2 = DecodeCompressedPosition(triIdx.z, md);

        const float3 worldPosition0 = mul(float4(posOS0, 1.0f), objectData.model).xyz;
        const float3 worldPosition1 = mul(float4(posOS1, 1.0f), objectData.model).xyz;
        const float3 worldPosition2 = mul(float4(posOS2, 1.0f), objectData.model).xyz;

        const float3 edgeFactors = ComputeReyesEdgeTessFactors(worldPosition0, worldPosition1, worldPosition2, camera);
        const float triangleFactor = max(edgeFactors.x, max(edgeFactors.y, edgeFactors.z));
        if (triangleFactor > bestTriangleFactor)
        {
            bestTriangleFactor = triangleFactor;
            bestTriangleIndex = triangleIndex;
            bestEdgeFactors = edgeFactors;
        }
    }

    return bestTriangleIndex;
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

    const float tessFactor = ComputeReyesTessFactor(
        meshletDesc.bounds.xyz,
        meshletDesc.bounds.w,
        CLodDescTriangleCount(meshletDesc),
        displacementSpan,
        objectData.model,
        meshInstance.skinnedBoundsScale,
        camera);
    const uint quantizedTessFactor = (uint)min(65535.0f, ceil(tessFactor * 256.0f));
    float3 seedEdgeFactors = float3(1.0f, 1.0f, 1.0f);
    const uint seedTriangleIndex = SelectReyesSeedTriangle(
        clusterIndex,
        objectData,
        camera,
        seedEdgeFactors);
    const uint splitConfig = ChooseReyesSplitConfig(seedEdgeFactors);
    const uint sourcePrimitiveAndSplitConfig = (seedTriangleIndex & 0xFFFFu) | ((splitConfig & 0xFFFFu) << 16u);

    if (tessFactor <= REYES_IMMEDIATE_DICE_TESS_THRESHOLD)
    {
        RWStructuredBuffer<uint> diceCounter = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_DICE_QUEUE_COUNTER_DESCRIPTOR_INDEX];
        RWStructuredBuffer<CLodReyesDiceQueueEntry> diceQueue = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_DICE_QUEUE_DESCRIPTOR_INDEX];
        RWStructuredBuffer<CLodReyesTelemetry> telemetryBuffer = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_TELEMETRY_DESCRIPTOR_INDEX];

        uint dst = 0u;
        InterlockedAdd(diceCounter[0], 1u, dst);
        MarkVisibleClusterOwnedByReyes(clusterIndex);
        diceQueue[dst].visibleClusterIndex = clusterIndex;
        diceQueue[dst].instanceID = instanceID;
        diceQueue[dst].localMeshletIndex = localMeshletIndex;
        diceQueue[dst].materialIndex = materialIndex;
        diceQueue[dst].viewID = viewID;
        diceQueue[dst].splitLevel = 0u;
        diceQueue[dst].quantizedTessFactor = quantizedTessFactor;
        diceQueue[dst].flags = commonFlags;
        diceQueue[dst].sourcePrimitiveAndSplitConfig = sourcePrimitiveAndSplitConfig;
        diceQueue[dst].domainVertex0Encoded = EncodeReyesBarycentrics(float3(1.0f, 0.0f, 0.0f));
        diceQueue[dst].domainVertex1Encoded = EncodeReyesBarycentrics(float3(0.0f, 1.0f, 0.0f));
        diceQueue[dst].domainVertex2Encoded = EncodeReyesBarycentrics(float3(0.0f, 0.0f, 1.0f));
        diceQueue[dst].tessTableConfigIndex = splitConfig;
        diceQueue[dst].reserved = 0u;
        InterlockedAdd(telemetryBuffer[0].immediateDiceQueueEntryCount, 1u);
        return;
    }

    RWStructuredBuffer<uint> splitCounter = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_SPLIT_QUEUE_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesSplitQueueEntry> splitQueue = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_SPLIT_QUEUE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesTelemetry> telemetryBuffer = ResourceDescriptorHeap[CLOD_REYES_CLASSIFY_TELEMETRY_DESCRIPTOR_INDEX];

    uint dst = 0u;
    InterlockedAdd(splitCounter[0], 1u, dst);
    MarkVisibleClusterOwnedByReyes(clusterIndex);
    splitQueue[dst].visibleClusterIndex = clusterIndex;
    splitQueue[dst].instanceID = instanceID;
    splitQueue[dst].localMeshletIndex = localMeshletIndex;
    splitQueue[dst].materialIndex = materialIndex;
    splitQueue[dst].viewID = viewID;
    splitQueue[dst].splitLevel = 0u;
    splitQueue[dst].quantizedTessFactor = quantizedTessFactor;
    splitQueue[dst].flags = commonFlags;
    splitQueue[dst].sourcePrimitiveAndSplitConfig = sourcePrimitiveAndSplitConfig;
    splitQueue[dst].domainVertex0Encoded = EncodeReyesBarycentrics(float3(1.0f, 0.0f, 0.0f));
    splitQueue[dst].domainVertex1Encoded = EncodeReyesBarycentrics(float3(0.0f, 1.0f, 0.0f));
    splitQueue[dst].domainVertex2Encoded = EncodeReyesBarycentrics(float3(0.0f, 0.0f, 1.0f));
}