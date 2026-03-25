#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/vertexFlags.hlsli"
#include "include/skinningCommon.hlsli"
#include "include/visibleClusterPacking.hlsli"
#include "include/clodPageAccess.hlsli"
#include "include/clodStructs.hlsli"
#include "include/reyesPatchCommon.hlsli"
#include "include/visibilityPacking.hlsli"
#include "PerPassRootConstants/clodReyesPatchRasterRootConstants.h"

static const uint REYES_PATCH_RASTER_GROUP_SIZE = 64u;

uint ReadPackedBits32(ByteAddressBuffer buf, uint startBit, uint bitCount)
{
    if (bitCount == 0u) return 0u;

    uint wordIndex = startBit >> 5;
    uint bitOffset = startBit & 31u;
    uint packed = buf.Load(wordIndex * 4u) >> bitOffset;
    if (bitOffset + bitCount > 32u)
    {
        packed |= buf.Load((wordIndex + 1u) * 4u) << (32u - bitOffset);
    }

    uint mask = (bitCount >= 32u) ? 0xffffffffu : ((1u << bitCount) - 1u);
    return packed & mask;
}

float3 DecodeCompressedPosition(
    uint meshletLocalVertex,
    uint positionBitstreamBase,
    uint positionBitOffset,
    uint bitsX,
    uint bitsY,
    uint bitsZ,
    uint quantExp,
    int3 minQ,
    uint pagePoolSlabDescriptorIndex)
{
    uint bitsPerVertex = bitsX + bitsY + bitsZ;
    uint bitCursor = positionBitstreamBase * 8u + positionBitOffset + meshletLocalVertex * bitsPerVertex;

    ByteAddressBuffer slab = ResourceDescriptorHeap[pagePoolSlabDescriptorIndex];
    uint px = ReadPackedBits32(slab, bitCursor, bitsX); bitCursor += bitsX;
    uint py = ReadPackedBits32(slab, bitCursor, bitsY); bitCursor += bitsY;
    uint pz = ReadPackedBits32(slab, bitCursor, bitsZ);

    int3 q = int3(px, py, pz) + minQ;
    float invScale = 1.0f / float(1u << quantExp);
    return float3(q) * invScale;
}

uint3 DecodeTriangle(ByteAddressBuffer slab, uint triStreamBase, uint triByteOffset, uint triLocalIndex)
{
    uint triOffset = triStreamBase + triByteOffset + triLocalIndex * 3u;
    uint alignedOffset = (triOffset / 4u) * 4u;
    uint firstWord = slab.Load(alignedOffset);
    uint byteOffset = triOffset % 4u;

    uint b0 = (firstWord >> (byteOffset * 8u)) & 0xFFu;
    uint b1;
    uint b2;

    if (byteOffset <= 1u)
    {
        b1 = (firstWord >> ((byteOffset + 1u) * 8u)) & 0xFFu;
        b2 = (firstWord >> ((byteOffset + 2u) * 8u)) & 0xFFu;
    }
    else if (byteOffset == 2u)
    {
        b1 = (firstWord >> 24u) & 0xFFu;
        uint secondWord = slab.Load(alignedOffset + 4u);
        b2 = secondWord & 0xFFu;
    }
    else
    {
        uint secondWord = slab.Load(alignedOffset + 4u);
        b1 = secondWord & 0xFFu;
        b2 = (secondWord >> 8u) & 0xFFu;
    }

    return uint3(b0, b1, b2);
}

SkinningInfluences DecodePackedJoints(
    uint meshletLocalVertex,
    CLodPageHeader hdr,
    CLodMeshletDescriptor desc,
    uint pageByteOffset,
    uint pagePoolSlabDescriptorIndex)
{
    SkinningInfluences skinning = (SkinningInfluences)0;
    if ((hdr.attributeMask & CLOD_PAGE_ATTRIBUTE_JOINTS) == 0u)
    {
        return skinning;
    }

    ByteAddressBuffer slab = ResourceDescriptorHeap[pagePoolSlabDescriptorIndex];
    uint addr = pageByteOffset + hdr.jointArrayOffset + (desc.vertexAttributeOffset + meshletLocalVertex) * 32u;
    skinning.joints0 = LoadUint4(addr, slab);
    skinning.joints1 = LoadUint4(addr + 16u, slab);
    return skinning;
}

SkinningInfluences DecodePackedWeights(
    uint meshletLocalVertex,
    CLodPageHeader hdr,
    CLodMeshletDescriptor desc,
    uint pageByteOffset,
    uint pagePoolSlabDescriptorIndex,
    SkinningInfluences skinning)
{
    if ((hdr.attributeMask & CLOD_PAGE_ATTRIBUTE_WEIGHTS) == 0u)
    {
        return skinning;
    }

    ByteAddressBuffer slab = ResourceDescriptorHeap[pagePoolSlabDescriptorIndex];
    uint addr = pageByteOffset + hdr.weightArrayOffset + (desc.vertexAttributeOffset + meshletLocalVertex) * 32u;
    skinning.weights0 = LoadFloat4(addr, slab);
    skinning.weights1 = LoadFloat4(addr + 16u, slab);
    return skinning;
}

float3 DecodeSkinnedPosition(
    uint meshletLocalVertex,
    CLodPageHeader hdr,
    CLodMeshletDescriptor desc,
    uint pageByteOffset,
    uint pagePoolSlabDescriptorIndex,
    uint vertexFlags,
    uint skinningInstanceSlot)
{
    float3 localPos = DecodeCompressedPosition(
        meshletLocalVertex,
        pageByteOffset + hdr.positionBitstreamOffset,
        desc.positionBitOffset,
        CLodDescBitsX(desc),
        CLodDescBitsY(desc),
        CLodDescBitsZ(desc),
        hdr.compressedPositionQuantExp,
        int3(desc.minQx, desc.minQy, desc.minQz),
        pagePoolSlabDescriptorIndex);

    if ((vertexFlags & VERTEX_SKINNED) != 0u)
    {
        SkinningInfluences skinning = DecodePackedJoints(meshletLocalVertex, hdr, desc, pageByteOffset, pagePoolSlabDescriptorIndex);
        skinning = DecodePackedWeights(meshletLocalVertex, hdr, desc, pageByteOffset, pagePoolSlabDescriptorIndex, skinning);
        localPos = mul(float4(localPos, 1.0f), BuildSkinMatrix(skinningInstanceSlot, skinning)).xyz;
    }

    return localPos;
}

float3 ReyesInterpolatePosition(float3 p0, float3 p1, float3 p2, float3 bary)
{
    return p0 * bary.x + p1 * bary.y + p2 * bary.z;
}

void ReyesRasterizeMicroTriangle(
    RWTexture2D<uint64_t> visBuffer,
    uint2 visDims,
    ClodViewRasterInfo viewRasterInfo,
    float4 clip0,
    float4 clip1,
    float4 clip2,
    float depth0,
    float depth1,
    float depth2,
    uint patchVisibilityIndex,
    uint microTriangleIndex)
{
    if (clip0.w <= 0.0f || clip1.w <= 0.0f || clip2.w <= 0.0f)
    {
        return;
    }

    const float2 ndc0 = clip0.xy / clip0.w;
    const float2 ndc1 = clip1.xy / clip1.w;
    const float2 ndc2 = clip2.xy / clip2.w;

    const float visWidth = float(viewRasterInfo.scissorMaxX - viewRasterInfo.scissorMinX);
    const float visHeight = float(viewRasterInfo.scissorMaxY - viewRasterInfo.scissorMinY);
    const float scissorMinXf = float(viewRasterInfo.scissorMinX);
    const float scissorMinYf = float(viewRasterInfo.scissorMinY);
    float2 s0 = float2((ndc0.x + 1.0f) * 0.5f * visWidth + scissorMinXf, (1.0f - ndc0.y) * 0.5f * visHeight + scissorMinYf);
    float2 s1 = float2((ndc1.x + 1.0f) * 0.5f * visWidth + scissorMinXf, (1.0f - ndc1.y) * 0.5f * visHeight + scissorMinYf);
    float2 s2 = float2((ndc2.x + 1.0f) * 0.5f * visWidth + scissorMinXf, (1.0f - ndc2.y) * 0.5f * visHeight + scissorMinYf);

    float2 e01 = s1 - s0;
    float2 e02 = s2 - s0;
    float twiceArea = e01.x * e02.y - e01.y * e02.x;
    if (abs(twiceArea) <= 1e-8f)
    {
        return;
    }

    if (twiceArea > 0.0f)
    {
        float2 tmpPos = s1;
        s1 = s2;
        s2 = tmpPos;

        float tmpDepth = depth1;
        depth1 = depth2;
        depth2 = tmpDepth;

        e01 = s1 - s0;
        e02 = s2 - s0;
        twiceArea = e01.x * e02.y - e01.y * e02.x;
    }
    else if (twiceArea >= 0.0f)
    {
        return;
    }

    const float invTwiceArea = -1.0f / twiceArea;
    const float2 bbMinF = min(min(s0, s1), s2);
    const float2 bbMaxF = max(max(s0, s1), s2);
    int2 minPx = int2(floor(bbMinF));
    int2 maxPx = int2(floor(bbMaxF));
    minPx = max(minPx, int2(viewRasterInfo.scissorMinX, viewRasterInfo.scissorMinY));
    maxPx = min(maxPx, int2(int(viewRasterInfo.scissorMaxX) - 1, int(viewRasterInfo.scissorMaxY) - 1));
    minPx = max(minPx, int2(0, 0));
    maxPx = min(maxPx, int2(int(visDims.x) - 1, int(visDims.y) - 1));
    if (minPx.x > maxPx.x || minPx.y > maxPx.y)
    {
        return;
    }

    const float2 origin = float2(float(minPx.x) + 0.5f, float(minPx.y) + 0.5f);
    const float2 e12 = s2 - s1;
    const float2 e20 = s0 - s2;
    const float row_b0 = ((origin.x - s1.x) * e12.y - (origin.y - s1.y) * e12.x) * invTwiceArea;
    const float row_b1 = ((origin.x - s2.x) * e20.y - (origin.y - s2.y) * e20.x) * invTwiceArea;
    const float dx_b0 = e12.y * invTwiceArea;
    const float dx_b1 = e20.y * invTwiceArea;
    const float dy_b0 = -e12.x * invTwiceArea;
    const float dy_b1 = -e20.x * invTwiceArea;

    float scanline_b0 = row_b0;
    float scanline_b1 = row_b1;
    [loop]
    for (int py = minPx.y; py <= maxPx.y; ++py)
    {
        float b0 = scanline_b0;
        float b1 = scanline_b1;
        [loop]
        for (int px = minPx.x; px <= maxPx.x; ++px)
        {
            const float b2 = 1.0f - b0 - b1;
            if (b0 >= 0.0f && b1 >= 0.0f && b2 >= 0.0f)
            {
                const float depth = b0 * depth0 + b1 * depth1 + b2 * depth2;
                const uint64_t visKey = PackVisKey(depth, patchVisibilityIndex, microTriangleIndex);
                InterlockedMin(visBuffer[uint2(px, py)], visKey);
            }

            b0 += dx_b0;
            b1 += dx_b1;
        }

        scanline_b0 += dy_b0;
        scanline_b1 += dy_b1;
    }
}

[shader("compute")]
[numthreads(REYES_PATCH_RASTER_GROUP_SIZE, 1, 1)]
void ReyesPatchRasterCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<uint> diceQueueCounter = ResourceDescriptorHeap[CLOD_REYES_PATCH_RASTER_DICE_QUEUE_COUNTER_DESCRIPTOR_INDEX];
    const uint diceCount = diceQueueCounter[0];
    const uint diceIndex = dispatchThreadId.x;
    if (diceIndex >= diceCount)
    {
        return;
    }

    StructuredBuffer<CLodReyesDiceQueueEntry> diceQueue = ResourceDescriptorHeap[CLOD_REYES_PATCH_RASTER_DICE_QUEUE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesTelemetry> telemetryBuffer = ResourceDescriptorHeap[CLOD_REYES_PATCH_RASTER_TELEMETRY_DESCRIPTOR_INDEX];
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_REYES_PATCH_RASTER_VIEW_RASTER_INFO_DESCRIPTOR_INDEX];
    ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[CLOD_REYES_PATCH_RASTER_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX];
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstances = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    StructuredBuffer<PerMeshBuffer> perMeshes = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<PerObjectBuffer> perObjects = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    StructuredBuffer<CullingCameraInfo> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];

    const CLodReyesDiceQueueEntry diceEntry = diceQueue[diceIndex];
    const ClodViewRasterInfo viewRasterInfo = viewRasterInfoBuffer[diceEntry.viewID];
    if (viewRasterInfo.visibilityUAVDescriptorIndex == 0xFFFFFFFFu)
    {
        return;
    }

    const uint3 packedCluster = CLodLoadVisibleClusterPacked(visibleClusters, diceEntry.visibleClusterIndex);
    const uint localMeshletIndex = CLodVisibleClusterLocalMeshletIndex(packedCluster);
    const uint pageSlabDescriptorIndex = CLodVisibleClusterPageSlabDescriptorIndex(packedCluster);
    const uint pageSlabByteOffset = CLodVisibleClusterPageSlabByteOffset(packedCluster);
    const CLodPageHeader hdr = LoadPageHeader(pageSlabDescriptorIndex, pageSlabByteOffset);
    const CLodMeshletDescriptor meshletDesc = LoadMeshletDescriptor(pageSlabDescriptorIndex, pageSlabByteOffset, hdr.descriptorOffset, localMeshletIndex);

    const PerMeshInstanceBuffer meshInstance = perMeshInstances[diceEntry.instanceID];
    const PerMeshBuffer perMesh = perMeshes[meshInstance.perMeshBufferIndex];
    const PerObjectBuffer objectData = perObjects[meshInstance.perObjectBufferIndex];
    const CullingCameraInfo camera = cameras[diceEntry.viewID];

    ByteAddressBuffer slab = ResourceDescriptorHeap[pageSlabDescriptorIndex];
    const uint sourceTriangleIndex = diceEntry.sourcePrimitiveAndSplitConfig & 0xFFFFu;
    if (sourceTriangleIndex >= CLodDescTriangleCount(meshletDesc))
    {
        return;
    }

    const uint3 sourceTriangle = DecodeTriangle(slab, pageSlabByteOffset + hdr.triangleStreamOffset, meshletDesc.triangleByteOffset, sourceTriangleIndex);
    const float3 sourcePosition0 = DecodeSkinnedPosition(sourceTriangle.x, hdr, meshletDesc, pageSlabByteOffset, pageSlabDescriptorIndex, perMesh.vertexFlags, meshInstance.skinningInstanceSlot);
    const float3 sourcePosition1 = DecodeSkinnedPosition(sourceTriangle.y, hdr, meshletDesc, pageSlabByteOffset, pageSlabDescriptorIndex, perMesh.vertexFlags, meshInstance.skinningInstanceSlot);
    const float3 sourcePosition2 = DecodeSkinnedPosition(sourceTriangle.z, hdr, meshletDesc, pageSlabByteOffset, pageSlabDescriptorIndex, perMesh.vertexFlags, meshInstance.skinningInstanceSlot);

    const float3 domain0 = ReyesDecodePatchBarycentrics(diceEntry.domainVertex0Encoded);
    const float3 domain1 = ReyesDecodePatchBarycentrics(diceEntry.domainVertex1Encoded);
    const float3 domain2 = ReyesDecodePatchBarycentrics(diceEntry.domainVertex2Encoded);
    const uint tessSegments = ReyesGetDicePatchSegments(diceEntry);
    const uint microTriangleCount = tessSegments * tessSegments;
    if (microTriangleCount == 0u || microTriangleCount > 128u)
    {
        return;
    }

    InterlockedAdd(telemetryBuffer[0].patchRasterizedPatchCount, 1u);
    InterlockedAdd(telemetryBuffer[0].patchRasterizedMicroTriangleCount, microTriangleCount);

    row_major matrix modelViewProjection = mul(objectData.model, camera.viewProjection);
    float4 modelViewZ = mul(objectData.model, camera.viewZ);

    const uint patchVisibilityIndex = CLOD_REYES_PATCH_RASTER_PATCH_INDEX_BASE + diceIndex;
    const uint visibilityDescriptorIndex = viewRasterInfo.visibilityUAVDescriptorIndex;
    RWTexture2D<uint64_t> visBuffer = ResourceDescriptorHeap[visibilityDescriptorIndex];
    uint2 visDims;
    visBuffer.GetDimensions(visDims.x, visDims.y);

    [loop]
    for (uint microTriangleIndex = 0u; microTriangleIndex < microTriangleCount; ++microTriangleIndex)
    {
        float3 patchBary0;
        float3 patchBary1;
        float3 patchBary2;
        ReyesDecodeMicroTrianglePatchDomain(microTriangleIndex, tessSegments, patchBary0, patchBary1, patchBary2);

        const float3 sourceBary0 = ReyesComposeSourceBarycentricsPoint(patchBary0, domain0, domain1, domain2);
        const float3 sourceBary1 = ReyesComposeSourceBarycentricsPoint(patchBary1, domain0, domain1, domain2);
        const float3 sourceBary2 = ReyesComposeSourceBarycentricsPoint(patchBary2, domain0, domain1, domain2);

        const float3 patchPosition0 = ReyesInterpolatePosition(sourcePosition0, sourcePosition1, sourcePosition2, sourceBary0);
        const float3 patchPosition1 = ReyesInterpolatePosition(sourcePosition0, sourcePosition1, sourcePosition2, sourceBary1);
        const float3 patchPosition2 = ReyesInterpolatePosition(sourcePosition0, sourcePosition1, sourcePosition2, sourceBary2);

        const float4 clip0 = mul(float4(patchPosition0, 1.0f), modelViewProjection);
        const float4 clip1 = mul(float4(patchPosition1, 1.0f), modelViewProjection);
        const float4 clip2 = mul(float4(patchPosition2, 1.0f), modelViewProjection);
        const float depth0 = -dot(float4(patchPosition0, 1.0f), modelViewZ);
        const float depth1 = -dot(float4(patchPosition1, 1.0f), modelViewZ);
        const float depth2 = -dot(float4(patchPosition2, 1.0f), modelViewZ);

        ReyesRasterizeMicroTriangle(
            visBuffer,
            visDims,
            viewRasterInfo,
            clip0,
            clip1,
            clip2,
            depth0,
            depth1,
            depth2,
            patchVisibilityIndex,
            microTriangleIndex);
    }
}