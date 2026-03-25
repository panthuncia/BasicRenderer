#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/visibleClusterPacking.hlsli"
#include "include/clodPageAccess.hlsli"
#include "include/clodStructs.hlsli"
#include "include/clodResolveCommon.hlsli"
#include "PerPassRootConstants/clodReyesSplitRootConstants.h"

static const uint REYES_SPLIT_GROUP_SIZE = 64u;
static const uint REYES_MIN_QUANTIZED_TESS_FACTOR = 256u;
static const uint REYES_SPLIT_TERMINAL_TESS_FACTOR = 2048u;
static const uint REYES_SPLIT_CONFIG_UNIFORM4 = 0u;
static const uint REYES_SPLIT_CONFIG_EDGE01 = 1u;
static const uint REYES_SPLIT_CONFIG_EDGE12 = 2u;
static const uint REYES_SPLIT_CONFIG_EDGE20 = 3u;

uint ReyesEncodeBarycentrics(float3 barycentrics)
{
    uint u = min(0xFFFFu, (uint)round(saturate(barycentrics.y) * REYES_BARYCENTRIC_COORD_SCALE));
    uint v = min(0xFFFFu, (uint)round(saturate(barycentrics.z) * REYES_BARYCENTRIC_COORD_SCALE));
    return u | (v << 16u);
}

uint ReyesMidpointEncoded(uint encodedA, uint encodedB)
{
    return ReyesEncodeBarycentrics(0.5f * (ReyesDecodeBarycentrics(encodedA) + ReyesDecodeBarycentrics(encodedB)));
}

float3 ReyesInterpolateTriangle(float3 p0, float3 p1, float3 p2, float3 barycentrics)
{
    return p0 * barycentrics.x + p1 * barycentrics.y + p2 * barycentrics.z;
}

float3 ComputeReyesEdgeTessFactors(float3 worldPosition0, float3 worldPosition1, float3 worldPosition2, CullingCameraInfo camera)
{
    const float distance01 = max(camera.zNear, min(length(worldPosition0 - camera.positionWorldSpace.xyz), length(worldPosition1 - camera.positionWorldSpace.xyz)));
    const float distance12 = max(camera.zNear, min(length(worldPosition1 - camera.positionWorldSpace.xyz), length(worldPosition2 - camera.positionWorldSpace.xyz)));
    const float distance20 = max(camera.zNear, min(length(worldPosition2 - camera.positionWorldSpace.xyz), length(worldPosition0 - camera.positionWorldSpace.xyz)));

    const float edge01 = length(worldPosition0 - worldPosition1);
    const float edge12 = length(worldPosition1 - worldPosition2);
    const float edge20 = length(worldPosition2 - worldPosition0);

    const float scale = camera.projY * 1080.0f * 0.03125f;
    return max(float3(1.0f, 1.0f, 1.0f), float3(edge01 / distance01, edge12 / distance12, edge20 / distance20) * scale);
}

uint ChooseReyesSplitConfig(float3 edgeFactors)
{
    const float maxFactor = max(edgeFactors.x, max(edgeFactors.y, edgeFactors.z));
    const float minFactor = min(edgeFactors.x, min(edgeFactors.y, edgeFactors.z));
    if (maxFactor <= 8.0f)
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

CLodReyesSplitQueueEntry ReyesMakeChildEntry(CLodReyesSplitQueueEntry parent, uint splitConfig, uint childIndex, uint nextSplitLevel, uint nextQuantizedTessFactor)
{
    const uint midpoint01 = ReyesMidpointEncoded(parent.domainVertex0Encoded, parent.domainVertex1Encoded);
    const uint midpoint12 = ReyesMidpointEncoded(parent.domainVertex1Encoded, parent.domainVertex2Encoded);
    const uint midpoint20 = ReyesMidpointEncoded(parent.domainVertex2Encoded, parent.domainVertex0Encoded);

    const uint parentSourcePrimitive = parent.sourcePrimitiveAndSplitConfig & 0xFFFFu;

    CLodReyesSplitQueueEntry child = parent;
    child.splitLevel = nextSplitLevel;
    child.quantizedTessFactor = nextQuantizedTessFactor;
    child.sourcePrimitiveAndSplitConfig = parentSourcePrimitive;

    if (splitConfig == REYES_SPLIT_CONFIG_EDGE01)
    {
        if (childIndex == 0u)
        {
            child.domainVertex0Encoded = parent.domainVertex0Encoded;
            child.domainVertex1Encoded = midpoint01;
            child.domainVertex2Encoded = parent.domainVertex2Encoded;
        }
        else
        {
            child.domainVertex0Encoded = midpoint01;
            child.domainVertex1Encoded = parent.domainVertex1Encoded;
            child.domainVertex2Encoded = parent.domainVertex2Encoded;
        }
    }
    else if (splitConfig == REYES_SPLIT_CONFIG_EDGE12)
    {
        if (childIndex == 0u)
        {
            child.domainVertex0Encoded = parent.domainVertex1Encoded;
            child.domainVertex1Encoded = midpoint12;
            child.domainVertex2Encoded = parent.domainVertex0Encoded;
        }
        else
        {
            child.domainVertex0Encoded = midpoint12;
            child.domainVertex1Encoded = parent.domainVertex2Encoded;
            child.domainVertex2Encoded = parent.domainVertex0Encoded;
        }
    }
    else if (splitConfig == REYES_SPLIT_CONFIG_EDGE20)
    {
        if (childIndex == 0u)
        {
            child.domainVertex0Encoded = parent.domainVertex2Encoded;
            child.domainVertex1Encoded = midpoint20;
            child.domainVertex2Encoded = parent.domainVertex1Encoded;
        }
        else
        {
            child.domainVertex0Encoded = midpoint20;
            child.domainVertex1Encoded = parent.domainVertex0Encoded;
            child.domainVertex2Encoded = parent.domainVertex1Encoded;
        }
    }
    else
    {
        if (childIndex == 0u)
        {
            child.domainVertex0Encoded = parent.domainVertex0Encoded;
            child.domainVertex1Encoded = midpoint01;
            child.domainVertex2Encoded = midpoint20;
        }
        else if (childIndex == 1u)
        {
            child.domainVertex0Encoded = midpoint01;
            child.domainVertex1Encoded = parent.domainVertex1Encoded;
            child.domainVertex2Encoded = midpoint12;
        }
        else if (childIndex == 2u)
        {
            child.domainVertex0Encoded = midpoint20;
            child.domainVertex1Encoded = midpoint12;
            child.domainVertex2Encoded = parent.domainVertex2Encoded;
        }
        else
        {
            child.domainVertex0Encoded = midpoint01;
            child.domainVertex1Encoded = midpoint12;
            child.domainVertex2Encoded = midpoint20;
        }
    }

    return child;
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
    const uint splitPassIndex = CLOD_REYES_SPLIT_PASS_INDEX;
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

    const CLodReyesSplitQueueEntry splitEntry = splitQueue[splitIndex];
    InterlockedAdd(telemetryBuffer[0].splitInputCounts[splitPassIndex], 1u);

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

    const float3 bary0 = ReyesDecodeBarycentrics(splitEntry.domainVertex0Encoded);
    const float3 bary1 = ReyesDecodeBarycentrics(splitEntry.domainVertex1Encoded);
    const float3 bary2 = ReyesDecodeBarycentrics(splitEntry.domainVertex2Encoded);

    const float3 currentPosition0OS = ReyesInterpolateTriangle(sourcePosition0OS, sourcePosition1OS, sourcePosition2OS, bary0);
    const float3 currentPosition1OS = ReyesInterpolateTriangle(sourcePosition0OS, sourcePosition1OS, sourcePosition2OS, bary1);
    const float3 currentPosition2OS = ReyesInterpolateTriangle(sourcePosition0OS, sourcePosition1OS, sourcePosition2OS, bary2);

    const float3 currentPosition0WS = mul(float4(currentPosition0OS, 1.0f), objectData.model).xyz;
    const float3 currentPosition1WS = mul(float4(currentPosition1OS, 1.0f), objectData.model).xyz;
    const float3 currentPosition2WS = mul(float4(currentPosition2OS, 1.0f), objectData.model).xyz;

    const float3 edgeFactors = ComputeReyesEdgeTessFactors(currentPosition0WS, currentPosition1WS, currentPosition2WS, camera);
    const float maxEdgeFactor = max(edgeFactors.x, max(edgeFactors.y, edgeFactors.z));
    const uint selectedSplitConfig = ChooseReyesSplitConfig(edgeFactors);

    const uint nextSplitLevel = splitEntry.splitLevel + 1u;
    InterlockedMax(telemetryBuffer[0].deepestSplitLevelReached, nextSplitLevel);
    const uint nextQuantizedTessFactor = (uint)min(65535.0f, ceil(maxEdgeFactor * 256.0f));
    const bool routeToDice = (nextSplitLevel >= maxSplitPassCount) || (nextQuantizedTessFactor <= REYES_SPLIT_TERMINAL_TESS_FACTOR);

    if (routeToDice)
    {
        uint diceIndex = 0u;
        InterlockedAdd(diceQueueCounter[0], 1u, diceIndex);
        if (diceIndex >= queueCapacity)
        {
            InterlockedAdd(diceQueueOverflowCounter[0], 1u);
            InterlockedAdd(telemetryBuffer[0].diceQueueOverflowCounts[splitPassIndex], 1u);
            return;
        }

        CLodReyesDiceQueueEntry diceEntry;
        diceEntry.visibleClusterIndex = splitEntry.visibleClusterIndex;
        diceEntry.instanceID = splitEntry.instanceID;
        diceEntry.localMeshletIndex = splitEntry.localMeshletIndex;
        diceEntry.materialIndex = splitEntry.materialIndex;
        diceEntry.viewID = splitEntry.viewID;
        diceEntry.splitLevel = nextSplitLevel;
        diceEntry.quantizedTessFactor = nextQuantizedTessFactor;
        diceEntry.flags = splitEntry.flags;
        diceEntry.sourcePrimitiveAndSplitConfig = (sourceTriangleIndex & 0xFFFFu) | ((selectedSplitConfig & 0xFFFFu) << 16u);
        diceEntry.domainVertex0Encoded = splitEntry.domainVertex0Encoded;
        diceEntry.domainVertex1Encoded = splitEntry.domainVertex1Encoded;
        diceEntry.domainVertex2Encoded = splitEntry.domainVertex2Encoded;
        diceEntry.tessTableConfigIndex = selectedSplitConfig;
        diceEntry.reserved = 0u;
        diceQueue[diceIndex] = diceEntry;

        InterlockedAdd(telemetryBuffer[0].splitDiceOutputCounts[splitPassIndex], 1u);
        InterlockedAdd(telemetryBuffer[0].finalDiceQueueEntryCount, 1u);
        return;
    }

    const uint childCount = (selectedSplitConfig == REYES_SPLIT_CONFIG_UNIFORM4) ? 4u : 2u;
    uint outputSplitBaseIndex = 0u;
    InterlockedAdd(outputSplitQueueCounter[0], childCount, outputSplitBaseIndex);
    if (outputSplitBaseIndex >= queueCapacity)
    {
        InterlockedAdd(outputSplitQueueOverflowCounter[0], childCount);
        InterlockedAdd(telemetryBuffer[0].splitQueueOverflowCounts[splitPassIndex], childCount);
        return;
    }

    const uint maxWritableChildren = min(childCount, queueCapacity - outputSplitBaseIndex);
    for (uint childIndex = 0u; childIndex < maxWritableChildren; ++childIndex)
    {
        CLodReyesSplitQueueEntry childEntry = ReyesMakeChildEntry(splitEntry, selectedSplitConfig, childIndex, nextSplitLevel, nextQuantizedTessFactor);
        childEntry.sourcePrimitiveAndSplitConfig = (sourceTriangleIndex & 0xFFFFu) | ((selectedSplitConfig & 0xFFFFu) << 16u);
        outputSplitQueue[outputSplitBaseIndex + childIndex] = childEntry;
    }

    if (maxWritableChildren < childCount)
    {
        const uint overflowCount = childCount - maxWritableChildren;
        InterlockedAdd(outputSplitQueueOverflowCounter[0], overflowCount);
        InterlockedAdd(telemetryBuffer[0].splitQueueOverflowCounts[splitPassIndex], overflowCount);
    }

    InterlockedAdd(telemetryBuffer[0].splitChildOutputCounts[splitPassIndex], maxWritableChildren);
}