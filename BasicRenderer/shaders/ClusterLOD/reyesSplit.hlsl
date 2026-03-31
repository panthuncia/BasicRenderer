#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/visibleClusterPacking.hlsli"
#include "include/clodPageAccess.hlsli"
#include "include/clodStructs.hlsli"
#include "include/clodResolveCommon.hlsli"
#include "include/reyesPatchCommon.hlsli"
#include "PerPassRootConstants/clodReyesSplitRootConstants.h"

static const uint REYES_SPLIT_GROUP_SIZE = 64u;
static const uint REYES_SPLIT_TELEMETRY_PASS_COUNT = 4u;

float3 ReyesInterpolateTriangle(float3 p0, float3 p1, float3 p2, float3 barycentrics)
{
    precise float3 result = p0 * barycentrics.x + p1 * barycentrics.y + p2 * barycentrics.z;
    return result;
}

float3 ComputeReyesEdgeTessFactors(float3 worldPosition0, float3 worldPosition1, float3 worldPosition2, CullingCameraInfo camera)
{
    const float distance01 = max(camera.zNear, min(length(worldPosition0 - camera.positionWorldSpace.xyz), length(worldPosition1 - camera.positionWorldSpace.xyz)));
    const float distance12 = max(camera.zNear, min(length(worldPosition1 - camera.positionWorldSpace.xyz), length(worldPosition2 - camera.positionWorldSpace.xyz)));
    const float distance20 = max(camera.zNear, min(length(worldPosition2 - camera.positionWorldSpace.xyz), length(worldPosition0 - camera.positionWorldSpace.xyz)));

    const float edge01 = length(worldPosition0 - worldPosition1);
    const float edge12 = length(worldPosition1 - worldPosition2);
    const float edge20 = length(worldPosition2 - worldPosition0);

    const float scale = camera.projY * REYES_SCREEN_SCALE_REFERENCE * REYES_PROJECTED_PIXEL_TO_TESS_FACTOR_SCALE;
    return max(float3(1.0f, 1.0f, 1.0f), float3(edge01 / distance01, edge12 / distance12, edge20 / distance20) * scale);
}

// Compute per-edge split factors: how many segments to split each edge into,
// such that after splitting, each child's edge factors fit within REYES_TESS_TABLE_MAX_SEGMENTS.
uint3 ComputeReyesSplitFactors(float3 edgeFactors)
{
    return clamp(
        uint3(ceil(edgeFactors / float(REYES_TESS_TABLE_MAX_SEGMENTS))),
        uint3(1u, 1u, 1u),
        uint3(REYES_TESS_TABLE_MAX_SEGMENTS, REYES_TESS_TABLE_MAX_SEGMENTS, REYES_TESS_TABLE_MAX_SEGMENTS));
}

[shader("compute")]
[numthreads(REYES_SPLIT_GROUP_SIZE, 1, 1)]
void ReyesSplitCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<uint> splitQueueCounter = ResourceDescriptorHeap[CLOD_REYES_SPLIT_INPUT_QUEUE_COUNTER_DESCRIPTOR_INDEX];
    const uint splitCount = splitQueueCounter[0];
    const uint splitIndex = dispatchThreadId.x;
    if (splitIndex >= splitCount)
    {
        return;
    }

    const uint queueCapacity = CLOD_REYES_SPLIT_QUEUE_CAPACITY;
    const uint maxSplitPassCount = CLOD_REYES_SPLIT_MAX_PASS_COUNT;
    ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[CLOD_REYES_SPLIT_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodReyesSplitQueueEntry> splitQueue = ResourceDescriptorHeap[CLOD_REYES_SPLIT_INPUT_QUEUE_DESCRIPTOR_INDEX];
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstances = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjects = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    StructuredBuffer<CullingCameraInfo> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
    RWStructuredBuffer<uint> outputSplitQueueCounter = ResourceDescriptorHeap[CLOD_REYES_SPLIT_OUTPUT_SPLIT_QUEUE_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> outputSplitQueueOverflowCounter = ResourceDescriptorHeap[CLOD_REYES_SPLIT_OUTPUT_SPLIT_QUEUE_OVERFLOW_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesSplitQueueEntry> outputSplitQueue = ResourceDescriptorHeap[CLOD_REYES_SPLIT_OUTPUT_SPLIT_QUEUE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> diceQueueCounter = ResourceDescriptorHeap[CLOD_REYES_SPLIT_OUTPUT_DICE_QUEUE_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> diceQueueOverflowCounter = ResourceDescriptorHeap[CLOD_REYES_SPLIT_OUTPUT_DICE_QUEUE_OVERFLOW_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesDiceQueueEntry> diceQueue = ResourceDescriptorHeap[CLOD_REYES_SPLIT_OUTPUT_DICE_QUEUE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesTelemetry> telemetryBuffer = ResourceDescriptorHeap[CLOD_REYES_SPLIT_TELEMETRY_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodReyesTessTableConfigEntry> tessTableConfigs = ResourceDescriptorHeap[CLOD_REYES_SPLIT_TESS_TABLE_CONFIGS_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> tessTableVertices = ResourceDescriptorHeap[CLOD_REYES_SPLIT_TESS_TABLE_VERTICES_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> tessTableTriangles = ResourceDescriptorHeap[CLOD_REYES_SPLIT_TESS_TABLE_TRIANGLES_DESCRIPTOR_INDEX];

    const CLodReyesSplitQueueEntry splitEntry = splitQueue[splitIndex];
    const uint splitPassTelemetryIndex = min(splitEntry.splitLevel, REYES_SPLIT_TELEMETRY_PASS_COUNT - 1u);
    InterlockedAdd(telemetryBuffer[0].splitInputCounts[splitPassTelemetryIndex], 1u);
    InterlockedMax(telemetryBuffer[0].deepestSplitLevelReached, splitEntry.splitLevel);

    const uint sourceTriangleIndex = splitEntry.sourcePrimitiveAndSplitConfig & 0xFFFFu;
    const uint3 packedCluster = CLodLoadVisibleClusterPacked(visibleClusters, splitEntry.visibleClusterIndex);
    const uint pageSlabDescriptorIndex = CLodVisibleClusterPageSlabDescriptorIndex(packedCluster);
    const uint pageSlabByteOffset = CLodVisibleClusterPageSlabByteOffset(packedCluster);
    const CLodPageHeader hdr = LoadPageHeader(pageSlabDescriptorIndex, pageSlabByteOffset);
    const CLodMeshletDescriptor meshletDesc = LoadMeshletDescriptor(pageSlabDescriptorIndex, pageSlabByteOffset, hdr.descriptorOffset, splitEntry.localMeshletIndex);

    MeshletResolveData md = (MeshletResolveData)0;
    md.drawcallAndMeshlet = uint2(splitEntry.instanceID, splitEntry.localMeshletIndex);
    md.vertexCount = CLodDescVertexCount(meshletDesc);
    md.triangleCount = CLodDescTriangleCount(meshletDesc);
    md.bitsX = CLodDescBitsX(meshletDesc);
    md.bitsY = CLodDescBitsY(meshletDesc);
    md.bitsZ = CLodDescBitsZ(meshletDesc);
    md.minQ = int3(meshletDesc.minQx, meshletDesc.minQy, meshletDesc.minQz);
    md.positionBitOffset = meshletDesc.positionBitOffset;
    md.vertexAttributeOffset = meshletDesc.vertexAttributeOffset;
    md.triangleByteOffset = meshletDesc.triangleByteOffset;
    md.pageAttributeMask = hdr.attributeMask;
    md.uvSetCount = hdr.uvSetCount;
    md.pageByteOffset = pageSlabByteOffset;
    md.uvDescriptorBase = pageSlabByteOffset + hdr.uvDescriptorOffset;
    md.uvBitstreamDirectoryBase = pageSlabByteOffset + hdr.uvBitstreamDirectoryOffset;
    md.positionBitstreamBase = pageSlabByteOffset + hdr.positionBitstreamOffset;
    md.normalArrayBase = pageSlabByteOffset + hdr.normalArrayOffset;
    md.colorArrayBase = pageSlabByteOffset + hdr.colorArrayOffset;
    md.jointArrayBase = pageSlabByteOffset + hdr.jointArrayOffset;
    md.weightArrayBase = pageSlabByteOffset + hdr.weightArrayOffset;
    md.triangleStreamBase = pageSlabByteOffset + hdr.triangleStreamOffset;
    md.compressedPositionQuantExp = hdr.compressedPositionQuantExp;
    md.pagePoolSlabDescriptorIndex = pageSlabDescriptorIndex;

    const PerMeshInstanceBuffer meshInstance = perMeshInstances[splitEntry.instanceID];
    const PerObjectBuffer objectData = perObjects[meshInstance.perObjectBufferIndex];
    const CullingCameraInfo camera = cameras[splitEntry.viewID];

    const uint3 sourceTriangle = DecodeTriangleCompact(min(sourceTriangleIndex, max(0u, md.triangleCount - 1u)), md);
    const float3 sourcePosition0OS = DecodeCompressedPosition(sourceTriangle.x, md);
    const float3 sourcePosition1OS = DecodeCompressedPosition(sourceTriangle.y, md);
    const float3 sourcePosition2OS = DecodeCompressedPosition(sourceTriangle.z, md);

    const float3 bary0 = ReyesPatchDomainUVToBarycentrics(splitEntry.domainVertex0UV);
    const float3 bary1 = ReyesPatchDomainUVToBarycentrics(splitEntry.domainVertex1UV);
    const float3 bary2 = ReyesPatchDomainUVToBarycentrics(splitEntry.domainVertex2UV);
    if (!ReyesPatchDomainHasValidSimplex(bary0, bary1, bary2))
    {
        InterlockedAdd(telemetryBuffer[0].invalidSplitPatchDomainCount, 1u);
        return;
    }

    const float3 currentPosition0OS = ReyesInterpolateTriangle(sourcePosition0OS, sourcePosition1OS, sourcePosition2OS, bary0);
    const float3 currentPosition1OS = ReyesInterpolateTriangle(sourcePosition0OS, sourcePosition1OS, sourcePosition2OS, bary1);
    const float3 currentPosition2OS = ReyesInterpolateTriangle(sourcePosition0OS, sourcePosition1OS, sourcePosition2OS, bary2);

    const float3 currentPosition0WS = mul(float4(currentPosition0OS, 1.0f), objectData.model).xyz;
    const float3 currentPosition1WS = mul(float4(currentPosition1OS, 1.0f), objectData.model).xyz;
    const float3 currentPosition2WS = mul(float4(currentPosition2OS, 1.0f), objectData.model).xyz;

    const float3 edgeFactors = ComputeReyesEdgeTessFactors(currentPosition0WS, currentPosition1WS, currentPosition2WS, camera);
    const float maxEdgeFactor = max(edgeFactors.x, max(edgeFactors.y, edgeFactors.z));

    const uint nextSplitLevel = splitEntry.splitLevel + 1u;
    const uint nextQuantizedTessFactor = (uint)min(65535.0f, ceil(maxEdgeFactor * 256.0f));
    InterlockedMax(telemetryBuffer[0].deepestSplitLevelReached, nextSplitLevel);

    const float2 originalDomainVertex0UV = splitEntry.domainVertex0UV;
    const float2 originalDomainVertex1UV = splitEntry.domainVertex1UV;
    const float2 originalDomainVertex2UV = splitEntry.domainVertex2UV;

    // Route to dice if edge factors fit within the tess table, or we've exhausted split passes.
    bool routeToDice = (nextSplitLevel >= maxSplitPassCount) || (maxEdgeFactor <= float(REYES_TESS_TABLE_MAX_SEGMENTS));
    bool collapseFallbackToDice = false;

    if (routeToDice)
    {
        uint diceIndex = 0u;
        InterlockedAdd(diceQueueCounter[0], 1u, diceIndex);
        if (diceIndex >= queueCapacity)
        {
            InterlockedAdd(diceQueueOverflowCounter[0], 1u);
            InterlockedAdd(telemetryBuffer[0].diceQueueOverflowCounts[splitPassTelemetryIndex], 1u);
            return;
        }

        float2 domainVertex0UV = originalDomainVertex0UV;
        float2 domainVertex1UV = originalDomainVertex1UV;
        float2 domainVertex2UV = originalDomainVertex2UV;
        const uint tessTableConfigIndex = ReyesEncodeCanonicalTessTableConfig(edgeFactors, domainVertex0UV, domainVertex1UV, domainVertex2UV);

        CLodReyesDiceQueueEntry diceEntry;
        diceEntry.visibleClusterIndex = splitEntry.visibleClusterIndex;
        diceEntry.instanceID = splitEntry.instanceID;
        diceEntry.localMeshletIndex = splitEntry.localMeshletIndex;
        diceEntry.materialIndex = splitEntry.materialIndex;
        diceEntry.viewID = splitEntry.viewID;
        diceEntry.splitLevel = nextSplitLevel;
        diceEntry.quantizedTessFactor = nextQuantizedTessFactor;
        diceEntry.flags = splitEntry.flags;
        diceEntry.sourcePrimitiveAndSplitConfig = (sourceTriangleIndex & 0xFFFFu) | ((tessTableConfigIndex & 0xFFFFu) << 16u);
        diceEntry.domainVertex0UV = domainVertex0UV;
        diceEntry.domainVertex1UV = domainVertex1UV;
        diceEntry.domainVertex2UV = domainVertex2UV;
        diceEntry.tessTableConfigIndex = tessTableConfigIndex;
        diceEntry.reserved = 0u;
        diceQueue[diceIndex] = diceEntry;
        InterlockedAdd(telemetryBuffer[0].splitDiceOutputCounts[splitPassTelemetryIndex], 1u);
        InterlockedAdd(telemetryBuffer[0].finalDiceQueueEntryCount, 1u);
        return;
    }

    // --- Tessellation-table-based splitting ---
    // Compute per-edge split factors: ceil(edgeFactor / TESS_TABLE_MAX_SEGMENTS), clamped to [1, MAX_SEGMENTS].
    // This ensures each child sub-triangle's edge factors will fit within the tess table.
    // Because split factors are computed per-edge from edge-local data, two triangles sharing
    // an edge will always agree on the split factor for that edge, producing identical boundary vertices.
    const uint3 splitFactors = ComputeReyesSplitFactors(edgeFactors);

    // Look up the tess table config for these split factors against the current domain.
    // This canonicalizes the factors (rotates so largest is first) and applies the flip bit.
    float2 domainVertex0UV = splitEntry.domainVertex0UV;
    float2 domainVertex1UV = splitEntry.domainVertex1UV;
    float2 domainVertex2UV = splitEntry.domainVertex2UV;
    const uint splitConfigIndex = ReyesEncodeCanonicalTessTableConfig(
        float3(splitFactors), domainVertex0UV, domainVertex1UV, domainVertex2UV);

    const CLodReyesTessTableConfigEntry splitConfig = ReyesGetTessTableConfigEntry(tessTableConfigs, splitConfigIndex);
    const uint childCount = splitConfig.numTriangles;
    InterlockedAdd(telemetryBuffer[0].splitChildOutputCounts[splitPassTelemetryIndex], childCount);

    // Decode the parent's (potentially rotated/flipped) domain vertices for rebasing.
    const float3 parentDomain0 = ReyesPatchDomainUVToBarycentrics(domainVertex0UV);
    const float3 parentDomain1 = ReyesPatchDomainUVToBarycentrics(domainVertex1UV);
    const float3 parentDomain2 = ReyesPatchDomainUVToBarycentrics(domainVertex2UV);

    [loop]
    for (uint childIndex = 0u; childIndex < childCount; ++childIndex)
    {
        const uint3 triIndices = ReyesGetTessTableConfigTriangleVertexIndices(tessTableConfigs, tessTableTriangles, splitConfigIndex, childIndex);

        const float3 microBary0 = ReyesGetTessTableConfigVertexBarycentrics(tessTableConfigs, tessTableVertices, splitConfigIndex, triIndices.x);
        const float3 microBary1 = ReyesGetTessTableConfigVertexBarycentrics(tessTableConfigs, tessTableVertices, splitConfigIndex, triIndices.y);
        const float3 microBary2 = ReyesGetTessTableConfigVertexBarycentrics(tessTableConfigs, tessTableVertices, splitConfigIndex, triIndices.z);

        precise float3 childDomain0 = parentDomain0 * microBary0.x + parentDomain1 * microBary0.y + parentDomain2 * microBary0.z;
        precise float3 childDomain1 = parentDomain0 * microBary1.x + parentDomain1 * microBary1.y + parentDomain2 * microBary1.z;
        precise float3 childDomain2 = parentDomain0 * microBary2.x + parentDomain1 * microBary2.y + parentDomain2 * microBary2.z;

        if (!ReyesPatchDomainHasValidSimplex(childDomain0, childDomain1, childDomain2))
        {
            collapseFallbackToDice = true;
            routeToDice = true;
            break;
        }
    }

    if (routeToDice)
    {
        if (collapseFallbackToDice)
        {
            InterlockedAdd(telemetryBuffer[0].splitCollapseFallbackDiceCount, 1u);
        }

        uint diceIndex = 0u;
        InterlockedAdd(diceQueueCounter[0], 1u, diceIndex);
        if (diceIndex >= queueCapacity)
        {
            InterlockedAdd(diceQueueOverflowCounter[0], 1u);
            InterlockedAdd(telemetryBuffer[0].diceQueueOverflowCounts[splitPassTelemetryIndex], 1u);
            return;
        }

        domainVertex0UV = originalDomainVertex0UV;
        domainVertex1UV = originalDomainVertex1UV;
        domainVertex2UV = originalDomainVertex2UV;
        const uint tessTableConfigIndex = ReyesEncodeCanonicalTessTableConfig(edgeFactors, domainVertex0UV, domainVertex1UV, domainVertex2UV);

        CLodReyesDiceQueueEntry diceEntry;
        diceEntry.visibleClusterIndex = splitEntry.visibleClusterIndex;
        diceEntry.instanceID = splitEntry.instanceID;
        diceEntry.localMeshletIndex = splitEntry.localMeshletIndex;
        diceEntry.materialIndex = splitEntry.materialIndex;
        diceEntry.viewID = splitEntry.viewID;
        diceEntry.splitLevel = nextSplitLevel;
        diceEntry.quantizedTessFactor = nextQuantizedTessFactor;
        diceEntry.flags = splitEntry.flags;
        diceEntry.sourcePrimitiveAndSplitConfig = (sourceTriangleIndex & 0xFFFFu) | ((tessTableConfigIndex & 0xFFFFu) << 16u);
        diceEntry.domainVertex0UV = domainVertex0UV;
        diceEntry.domainVertex1UV = domainVertex1UV;
        diceEntry.domainVertex2UV = domainVertex2UV;
        diceEntry.tessTableConfigIndex = tessTableConfigIndex;
        diceEntry.reserved = 0u;
        diceQueue[diceIndex] = diceEntry;
        InterlockedAdd(telemetryBuffer[0].splitDiceOutputCounts[splitPassTelemetryIndex], 1u);
        InterlockedAdd(telemetryBuffer[0].finalDiceQueueEntryCount, 1u);
        return;
    }

    uint outputSplitBaseIndex = 0u;
    InterlockedAdd(outputSplitQueueCounter[0], childCount, outputSplitBaseIndex);
    if (outputSplitBaseIndex >= queueCapacity)
    {
        InterlockedAdd(outputSplitQueueOverflowCounter[0], childCount);
        InterlockedAdd(telemetryBuffer[0].splitQueueOverflowCounts[splitPassTelemetryIndex], childCount);
        return;
    }

    const uint maxWritableChildren = min(childCount, queueCapacity - outputSplitBaseIndex);

    for (uint childIndex = 0u; childIndex < maxWritableChildren; ++childIndex)
    {
        // Get the micro-triangle's vertex indices from the tess table.
        const uint3 triIndices = ReyesGetTessTableConfigTriangleVertexIndices(tessTableConfigs, tessTableTriangles, splitConfigIndex, childIndex);

        // Get each micro-triangle vertex's barycentrics within the split pattern.
        const float3 microBary0 = ReyesGetTessTableConfigVertexBarycentrics(tessTableConfigs, tessTableVertices, splitConfigIndex, triIndices.x);
        const float3 microBary1 = ReyesGetTessTableConfigVertexBarycentrics(tessTableConfigs, tessTableVertices, splitConfigIndex, triIndices.y);
        const float3 microBary2 = ReyesGetTessTableConfigVertexBarycentrics(tessTableConfigs, tessTableVertices, splitConfigIndex, triIndices.z);

        // Rebase: convert micro-triangle barycentrics through the parent domain
        // to get new domain vertices in the original source triangle's barycentric space.
        precise float3 childDomain0 = parentDomain0 * microBary0.x + parentDomain1 * microBary0.y + parentDomain2 * microBary0.z;
        precise float3 childDomain1 = parentDomain0 * microBary1.x + parentDomain1 * microBary1.y + parentDomain2 * microBary1.z;
        precise float3 childDomain2 = parentDomain0 * microBary2.x + parentDomain1 * microBary2.y + parentDomain2 * microBary2.z;

        CLodReyesSplitQueueEntry childEntry;
        childEntry.visibleClusterIndex = splitEntry.visibleClusterIndex;
        childEntry.instanceID = splitEntry.instanceID;
        childEntry.localMeshletIndex = splitEntry.localMeshletIndex;
        childEntry.materialIndex = splitEntry.materialIndex;
        childEntry.viewID = splitEntry.viewID;
        childEntry.splitLevel = nextSplitLevel;
        childEntry.quantizedTessFactor = nextQuantizedTessFactor;
        childEntry.flags = splitEntry.flags;
        childEntry.sourcePrimitiveAndSplitConfig = (sourceTriangleIndex & 0xFFFFu);
        childEntry.domainVertex0UV = ReyesPatchBarycentricsToUV(childDomain0);
        childEntry.domainVertex1UV = ReyesPatchBarycentricsToUV(childDomain1);
        childEntry.domainVertex2UV = ReyesPatchBarycentricsToUV(childDomain2);

        outputSplitQueue[outputSplitBaseIndex + childIndex] = childEntry;
    }

    if (maxWritableChildren < childCount)
    {
        const uint overflowCount = childCount - maxWritableChildren;
        InterlockedAdd(outputSplitQueueOverflowCounter[0], overflowCount);
        InterlockedAdd(telemetryBuffer[0].splitQueueOverflowCounts[splitPassTelemetryIndex], overflowCount);
    }
}