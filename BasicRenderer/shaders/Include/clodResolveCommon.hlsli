#ifndef __CLOD_RESOLVE_COMMON_HLSLI__
#define __CLOD_RESOLVE_COMMON_HLSLI__

//#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/skinningCommon.hlsli"
#include "include/meshletCommon.hlsli"
#include "include/utilities.hlsli"
#include "include/waveIntrinsicsHelpers.hlsli"
#include "include/visibilityPacking.hlsli"
#include "include/clodStructs.hlsli"
#include "include/clodPageAccess.hlsli"
#include "include/reyesPatchCommon.hlsli"
#include "include/visibleClusterPacking.hlsli"
#include "include/vertexLayout.hlsli"
#include "PerPassRootConstants/visUtilRootConstants.h"

#define CLOD_COMPRESSED_POSITIONS 1u
#define CLOD_COMPRESSED_NORMALS 4u

struct BarycentricDeriv
{
    float3 m_lambda;
    float3 m_ddx;
    float3 m_ddy;
};

struct ClodResolvedSample
{
    uint2 pixelCoords;
    float linearDepth;
    uint clusterIndex;
    uint meshletTriangleIndex;
    uint meshletIndex;
    float3 positionWS;
    float3 positionVS;
    float3 normalWSBase;
    float3 normalOS;
    float3 vertexColor;
    float3 dpdxWS;
    float3 dpdyWS;
    float2 motionVector;
    MaterialInfo materialInfo;
    uint materialFlags;
    MaterialInputs materialInputs;
};

BarycentricDeriv CalcFullBary(float4 pt0, float4 pt1, float4 pt2, float2 pixelNdc, float2 winSize)
{
    BarycentricDeriv ret = (BarycentricDeriv)0;

    float3 invW = rcp(float3(pt0.w, pt1.w, pt2.w));

    float2 ndc0 = pt0.xy * invW.x;
    float2 ndc1 = pt1.xy * invW.y;
    float2 ndc2 = pt2.xy * invW.z;

    float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
    ret.m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
    ret.m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
    float ddxSum = dot(ret.m_ddx, float3(1, 1, 1));
    float ddySum = dot(ret.m_ddy, float3(1, 1, 1));

    float2 deltaVec = pixelNdc - ndc0;
    float interpInvW = invW.x + deltaVec.x * ddxSum + deltaVec.y * ddySum;
    float interpW = rcp(interpInvW);

    ret.m_lambda.x = interpW * (invW[0] + deltaVec.x * ret.m_ddx.x + deltaVec.y * ret.m_ddy.x);
    ret.m_lambda.y = interpW * (0.0f + deltaVec.x * ret.m_ddx.y + deltaVec.y * ret.m_ddy.y);
    ret.m_lambda.z = interpW * (0.0f + deltaVec.x * ret.m_ddx.z + deltaVec.y * ret.m_ddy.z);

    ret.m_ddx *= (2.0f / winSize.x);
    ret.m_ddy *= (2.0f / winSize.y);
    ddxSum *= (2.0f / winSize.x);
    ddySum *= (2.0f / winSize.y);

    ret.m_ddy *= -1.0f;
    ddySum *= -1.0f;

    float interpW_ddx = 1.0f / (interpInvW + ddxSum);
    float interpW_ddy = 1.0f / (interpInvW + ddySum);

    ret.m_ddx = interpW_ddx * (ret.m_lambda * interpInvW + ret.m_ddx) - ret.m_lambda;
    ret.m_ddy = interpW_ddy * (ret.m_lambda * interpInvW + ret.m_ddy) - ret.m_lambda;

    return ret;
}

float3 InterpolateWithDeriv(BarycentricDeriv deriv, float v0, float v1, float v2)
{
    float3 mergedV = float3(v0, v1, v2);
    float3 ret;
    ret.x = dot(mergedV, deriv.m_lambda);
    ret.y = dot(mergedV, deriv.m_ddx);
    ret.z = dot(mergedV, deriv.m_ddy);
    return ret;
}

struct MeshletResolveData {
    uint2 drawcallAndMeshlet;
    uint2 objAndMesh;
    uint4 meshInfo;
    uint materialDataIndex;
    uint vertexCount;
    uint triangleCount;
    uint bitsX;
    uint bitsY;
    uint bitsZ;
    int3 minQ;
    uint positionBitOffset;
    uint vertexAttributeOffset;
    uint triangleByteOffset;
    uint pageAttributeMask;
    uint uvSetCount;
    uint uvDescriptorBase;
    uint uvBitstreamDirectoryBase;
    uint pageByteOffset;
    uint positionBitstreamBase;
    uint normalArrayBase;
    uint colorArrayBase;
    uint jointArrayBase;
    uint weightArrayBase;
    uint triangleStreamBase;
    uint skinningInstanceSlot;
    uint compressedPositionQuantExp;
    uint pagePoolSlabDescriptorIndex;
};

uint ReadPackedBits32_BA(ByteAddressBuffer buf, uint startBit, uint bitCount)
{
    if (bitCount == 0u)
    {
        return 0u;
    }

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

float2 DecodeCompressedUV(uint meshletLocalVertex, uint uvSetIndex, MeshletResolveData d)
{
    if (uvSetIndex >= d.uvSetCount)
    {
        return float2(0.0f, 0.0f);
    }

    CLodMeshletUvDescriptor uvDesc = LoadMeshletUvDescriptor(
        d.pagePoolSlabDescriptorIndex,
        d.pageByteOffset,
        d.uvDescriptorBase - d.pageByteOffset,
        d.uvSetCount,
        d.drawcallAndMeshlet.y,
        uvSetIndex);
    uint uvBitstreamBase = d.pageByteOffset + LoadPageUvBitstreamOffset(
        d.pagePoolSlabDescriptorIndex,
        d.pageByteOffset,
        d.uvBitstreamDirectoryBase - d.pageByteOffset,
        uvSetIndex);
    uint uvBitsU = CLodUvDescBitsU(uvDesc);
    uint uvBitsV = CLodUvDescBitsV(uvDesc);

    uint bitsPerVertex = uvBitsU + uvBitsV;
    uint bitCursor = uvBitstreamBase * 8u + uvDesc.uvBitOffset + meshletLocalVertex * bitsPerVertex;

    ByteAddressBuffer slab = ResourceDescriptorHeap[d.pagePoolSlabDescriptorIndex];
    uint encodedU = ReadPackedBits32_BA(slab, bitCursor, uvBitsU);
    bitCursor += uvBitsU;
    uint encodedV = ReadPackedBits32_BA(slab, bitCursor, uvBitsV);

    return float2(
        uvDesc.uvMinU + float(encodedU) * uvDesc.uvScaleU,
        uvDesc.uvMinV + float(encodedV) * uvDesc.uvScaleV);
}

void AppendClodMaterialUvSample(
    inout MaterialUvCache cache,
    uint uvSetIndex,
    uint3 triIdx,
    MeshletResolveData md,
    BarycentricDeriv bary)
{
    if (cache.count >= MATERIAL_MAX_UNIQUE_UV_SETS)
    {
        return;
    }

    float2 uv0 = DecodeCompressedUV(triIdx.x, uvSetIndex, md);
    float2 uv1 = DecodeCompressedUV(triIdx.y, uvSetIndex, md);
    float2 uv2 = DecodeCompressedUV(triIdx.z, uvSetIndex, md);

    float3 interpU = InterpolateWithDeriv(bary, uv0.x, uv1.x, uv2.x);
    float3 interpV = InterpolateWithDeriv(bary, uv0.y, uv1.y, uv2.y);

    MaterialUvSample sample = (MaterialUvSample)0;
    sample.uvSetIndex = uvSetIndex;
    sample.uv = float2(interpU.x, interpV.x);
    sample.dUVdx = float2(interpU.y, interpV.y);
    sample.dUVdy = float2(interpU.z, interpV.z);

    cache.samples[cache.count] = sample;
    cache.count++;
}

MaterialUvCache BuildClodMaterialUvCache(
    MaterialInfo materialInfo,
    uint materialFlags,
    MeshletResolveData md,
    uint3 triIdx,
    BarycentricDeriv bary)
{
    MaterialUvCache cache = (MaterialUvCache)0;

    [unroll]
    for (uint slot = 0u; slot < MATERIAL_TEXTURE_SLOT_COUNT; ++slot)
    {
        MaterialTextureSlot textureSlot = (MaterialTextureSlot)slot;
        if (!MaterialSlotEnabled(materialInfo, materialFlags, textureSlot))
        {
            continue;
        }

        uint uvSetIndex = MaterialSlotUvSetIndex(materialInfo, textureSlot);
        if (FindMaterialUvCacheIndex(cache, uvSetIndex) != MATERIAL_INVALID_UV_CACHE_INDEX)
        {
            continue;
        }

        AppendClodMaterialUvSample(cache, uvSetIndex, triIdx, md, bary);
    }

    return cache;
}

float3 DecodeCompressedPosition(uint meshletLocalVertex, MeshletResolveData d)
{
    uint bitsPerVertex = d.bitsX + d.bitsY + d.bitsZ;
    uint bitCursor = d.positionBitstreamBase * 8u + d.positionBitOffset + meshletLocalVertex * bitsPerVertex;

    ByteAddressBuffer slab = ResourceDescriptorHeap[d.pagePoolSlabDescriptorIndex];
    uint px = ReadPackedBits32_BA(slab, bitCursor, d.bitsX);
    bitCursor += d.bitsX;
    uint py = ReadPackedBits32_BA(slab, bitCursor, d.bitsY);
    bitCursor += d.bitsY;
    uint pz = ReadPackedBits32_BA(slab, bitCursor, d.bitsZ);

    int3 q = int3(px, py, pz) + d.minQ;
    float invScale = 1.0f / float(1u << d.compressedPositionQuantExp);
    return float3(q) * invScale;
}

float2 UnpackSnorm16x2(uint packed)
{
    int signedPacked = asint(packed);
    int x = (signedPacked << 16) >> 16;
    int y = signedPacked >> 16;
    float sx = max(-1.0f, (float)x / 32767.0f);
    float sy = max(-1.0f, (float)y / 32767.0f);
    return float2(sx, sy);
}

float3 OctDecodeNormal(float2 e)
{
    float3 v = float3(e.x, e.y, 1.0f - abs(e.x) - abs(e.y));
    if (v.z < 0.0f)
    {
        float2 folded = (1.0f - abs(v.yx)) * float2(v.x >= 0.0f ? 1.0f : -1.0f, v.y >= 0.0f ? 1.0f : -1.0f);
        v.x = folded.x;
        v.y = folded.y;
    }
    return normalize(v);
}

float3 DecodeCompressedNormal(uint meshletLocalVertex, MeshletResolveData d)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[d.pagePoolSlabDescriptorIndex];
    uint addr = d.normalArrayBase + (d.vertexAttributeOffset + meshletLocalVertex) * 4u;
    uint packed = slab.Load(addr);
    return OctDecodeNormal(UnpackSnorm16x2(packed));
}

float3 DecodeCompressedColor(uint meshletLocalVertex, MeshletResolveData d)
{
    ByteAddressBuffer slab = ResourceDescriptorHeap[d.pagePoolSlabDescriptorIndex];
    uint addr = d.colorArrayBase + (d.vertexAttributeOffset + meshletLocalVertex) * 4u;
    uint packed = slab.Load(addr);
    return float3(
        float(packed & 0xFFu) / 255.0f,
        float((packed >> 8u) & 0xFFu) / 255.0f,
        float((packed >> 16u) & 0xFFu) / 255.0f);
}

SkinningInfluences DecodePackedJoints(uint meshletLocalVertex, MeshletResolveData d)
{
    SkinningInfluences skinning;
    skinning.joints0 = uint4(0, 0, 0, 0);
    skinning.joints1 = uint4(0, 0, 0, 0);
    skinning.weights0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
    skinning.weights1 = float4(0.0f, 0.0f, 0.0f, 0.0f);

    if ((d.pageAttributeMask & CLOD_PAGE_ATTRIBUTE_JOINTS) == 0u)
    {
        return skinning;
    }

    ByteAddressBuffer slab = ResourceDescriptorHeap[d.pagePoolSlabDescriptorIndex];
    uint addr = d.jointArrayBase + (d.vertexAttributeOffset + meshletLocalVertex) * 32u;
    skinning.joints0 = LoadUint4(addr, slab);
    skinning.joints1 = LoadUint4(addr + 16u, slab);
    return skinning;
}

SkinningInfluences DecodePackedWeights(uint meshletLocalVertex, MeshletResolveData d, SkinningInfluences skinning)
{
    if ((d.pageAttributeMask & CLOD_PAGE_ATTRIBUTE_WEIGHTS) == 0u)
    {
        return skinning;
    }

    ByteAddressBuffer slab = ResourceDescriptorHeap[d.pagePoolSlabDescriptorIndex];
    uint addr = d.weightArrayBase + (d.vertexAttributeOffset + meshletLocalVertex) * 32u;
    skinning.weights0 = LoadFloat4(addr, slab);
    skinning.weights1 = LoadFloat4(addr + 16u, slab);
    return skinning;
}

void ApplyClodSkinning(uint meshletLocalVertex, MeshletResolveData d, inout float3 positionOS, inout float3 normalOS)
{
    if ((d.meshInfo.y & VERTEX_SKINNED) == 0u)
    {
        return;
    }

    SkinningInfluences skinning = DecodePackedJoints(meshletLocalVertex, d);
    skinning = DecodePackedWeights(meshletLocalVertex, d, skinning);
    float4x4 skinMatrix = BuildSkinMatrix(d.skinningInstanceSlot, skinning);
    positionOS = mul(float4(positionOS, 1.0f), skinMatrix).xyz;
    normalOS = mul(normalOS, (float3x3)skinMatrix);
}

MeshletResolveData LoadMeshletResolveData_Wave(uint clusterIndex)
{
    MeshletResolveData d = (MeshletResolveData)0;

    uint4 mask = WaveMatch(clusterIndex);
    uint leader = WaveFirstLaneFromMask(mask);
    bool isLeader = (WaveGetLaneIndex() == leader);

    if (isLeader)
    {
        ByteAddressBuffer visibleClusterBuffer = ResourceDescriptorHeap[VISBUF_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
        StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
        StructuredBuffer<PerMeshBuffer> perMeshBuffer =
            ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];

        const uint3 packedCluster = CLodLoadVisibleClusterPacked(visibleClusterBuffer, clusterIndex);
        d.drawcallAndMeshlet.x = CLodVisibleClusterInstanceID(packedCluster);
        d.drawcallAndMeshlet.y = CLodVisibleClusterLocalMeshletIndex(packedCluster);

        PerMeshInstanceBuffer inst = perMeshInstanceBuffer[d.drawcallAndMeshlet.x];
        d.objAndMesh = uint2(inst.perObjectBufferIndex, inst.perMeshBufferIndex);
        d.skinningInstanceSlot = inst.skinningInstanceSlot;

        PerMeshBuffer mesh = perMeshBuffer[d.objAndMesh.y];

        const uint pageSlabDesc = CLodVisibleClusterPageSlabDescriptorIndex(packedCluster);
        const uint pageSlabOff = CLodVisibleClusterPageSlabByteOffset(packedCluster);

        CLodPageHeader hdr = LoadPageHeader(pageSlabDesc, pageSlabOff);
        CLodMeshletDescriptor desc = LoadMeshletDescriptor(
            pageSlabDesc, pageSlabOff, hdr.descriptorOffset, d.drawcallAndMeshlet.y);

        d.meshInfo = uint4(mesh.vertexByteSize, mesh.vertexFlags, mesh.numVertices, 0);
        d.materialDataIndex = mesh.materialDataIndex;

        d.vertexCount = CLodDescVertexCount(desc);
        d.triangleCount = CLodDescTriangleCount(desc);
        d.bitsX = CLodDescBitsX(desc);
        d.bitsY = CLodDescBitsY(desc);
        d.bitsZ = CLodDescBitsZ(desc);
        d.minQ = int3(desc.minQx, desc.minQy, desc.minQz);
        d.positionBitOffset = desc.positionBitOffset;
        d.vertexAttributeOffset = desc.vertexAttributeOffset;
        d.triangleByteOffset = desc.triangleByteOffset;
        d.pageAttributeMask = hdr.attributeMask;
        d.uvSetCount = hdr.uvSetCount;

        d.pageByteOffset = pageSlabOff;
        d.uvDescriptorBase = pageSlabOff + hdr.uvDescriptorOffset;
        d.uvBitstreamDirectoryBase = pageSlabOff + hdr.uvBitstreamDirectoryOffset;
        d.positionBitstreamBase = pageSlabOff + hdr.positionBitstreamOffset;
        d.normalArrayBase = pageSlabOff + hdr.normalArrayOffset;
        d.colorArrayBase = pageSlabOff + hdr.colorArrayOffset;
        d.jointArrayBase = pageSlabOff + hdr.jointArrayOffset;
        d.weightArrayBase = pageSlabOff + hdr.weightArrayOffset;
        d.triangleStreamBase = pageSlabOff + hdr.triangleStreamOffset;

        d.compressedPositionQuantExp = hdr.compressedPositionQuantExp;
        d.pagePoolSlabDescriptorIndex = pageSlabDesc;
    }

    d.drawcallAndMeshlet = WaveReadLaneAt(d.drawcallAndMeshlet, leader);
    d.objAndMesh = WaveReadLaneAt(d.objAndMesh, leader);
    d.meshInfo = WaveReadLaneAt(d.meshInfo, leader);
    d.materialDataIndex = WaveReadLaneAt(d.materialDataIndex, leader);
    d.vertexCount = WaveReadLaneAt(d.vertexCount, leader);
    d.triangleCount = WaveReadLaneAt(d.triangleCount, leader);
    d.bitsX = WaveReadLaneAt(d.bitsX, leader);
    d.bitsY = WaveReadLaneAt(d.bitsY, leader);
    d.bitsZ = WaveReadLaneAt(d.bitsZ, leader);
    d.minQ.x = WaveReadLaneAt(d.minQ.x, leader);
    d.minQ.y = WaveReadLaneAt(d.minQ.y, leader);
    d.minQ.z = WaveReadLaneAt(d.minQ.z, leader);
    d.positionBitOffset = WaveReadLaneAt(d.positionBitOffset, leader);
    d.vertexAttributeOffset = WaveReadLaneAt(d.vertexAttributeOffset, leader);
    d.triangleByteOffset = WaveReadLaneAt(d.triangleByteOffset, leader);
    d.pageAttributeMask = WaveReadLaneAt(d.pageAttributeMask, leader);
    d.uvSetCount = WaveReadLaneAt(d.uvSetCount, leader);
    d.uvDescriptorBase = WaveReadLaneAt(d.uvDescriptorBase, leader);
    d.uvBitstreamDirectoryBase = WaveReadLaneAt(d.uvBitstreamDirectoryBase, leader);
    d.pageByteOffset = WaveReadLaneAt(d.pageByteOffset, leader);
    d.positionBitstreamBase = WaveReadLaneAt(d.positionBitstreamBase, leader);
    d.normalArrayBase = WaveReadLaneAt(d.normalArrayBase, leader);
    d.colorArrayBase = WaveReadLaneAt(d.colorArrayBase, leader);
    d.jointArrayBase = WaveReadLaneAt(d.jointArrayBase, leader);
    d.weightArrayBase = WaveReadLaneAt(d.weightArrayBase, leader);
    d.triangleStreamBase = WaveReadLaneAt(d.triangleStreamBase, leader);
    d.compressedPositionQuantExp = WaveReadLaneAt(d.compressedPositionQuantExp, leader);
    d.pagePoolSlabDescriptorIndex = WaveReadLaneAt(d.pagePoolSlabDescriptorIndex, leader);
    d.skinningInstanceSlot = WaveReadLaneAt(d.skinningInstanceSlot, leader);

    return d;
}

uint3 DecodeTriangleCompact(uint triLocalIndex, MeshletResolveData d)
{
    uint triOffset = d.triangleStreamBase + d.triangleByteOffset + triLocalIndex * 3u;

    ByteAddressBuffer slab = ResourceDescriptorHeap[d.pagePoolSlabDescriptorIndex];
    uint alignedOffset = (triOffset / 4u) * 4u;
    uint firstWord = slab.Load(alignedOffset);
    uint byteOffset = triOffset % 4u;

    uint b0 = (firstWord >> (byteOffset * 8u)) & 0xFFu;
    uint b1, b2;

    if (byteOffset <= 1u)
    {
        b1 = (firstWord >> ((byteOffset + 1u) * 8u)) & 0xFFu;
        b2 = (firstWord >> ((byteOffset + 2u) * 8u)) & 0xFFu;
    }
    else if (byteOffset == 2u)
    {
        b1 = (firstWord >> ((byteOffset + 1u) * 8u)) & 0xFFu;
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

float2 ComputeClodMotionVector(float3 posOS, float3 worldPosition, float4x4 prevModel, float4x4 unjitteredViewProj, float4x4 prevUnjitteredViewProj)
{
    float4 clipCur = mul(float4(worldPosition, 1.0f), unjitteredViewProj);
    float3 prevWorldPosition = mul(float4(posOS, 1.0f), prevModel).xyz;
    float4 clipPrev = mul(float4(prevWorldPosition, 1.0f), prevUnjitteredViewProj);

    float2 ndcCur = clipCur.xy / clipCur.w;
    float2 ndcPrev = clipPrev.xy / clipPrev.w;
    return ndcCur - ndcPrev;
}

float3 ReyesDecodeBarycentrics(uint encoded)
{
    return ReyesDecodePatchBarycentrics(encoded);
}

BarycentricDeriv ReyesComposeSourceBarycentrics(BarycentricDeriv patchBary, float3 domain0, float3 domain1, float3 domain2)
{
    BarycentricDeriv sourceBary = (BarycentricDeriv)0;
    sourceBary.m_lambda =
        domain0 * patchBary.m_lambda.x +
        domain1 * patchBary.m_lambda.y +
        domain2 * patchBary.m_lambda.z;
    sourceBary.m_ddx =
        domain0 * patchBary.m_ddx.x +
        domain1 * patchBary.m_ddx.y +
        domain2 * patchBary.m_ddx.z;
    sourceBary.m_ddy =
        domain0 * patchBary.m_ddy.x +
        domain1 * patchBary.m_ddy.y +
        domain2 * patchBary.m_ddy.z;
    return sourceBary;
}

bool ResolveClodSampleFromVisKeyWithFace(uint64_t vis, uint2 pixel, bool isBackface, out ClodResolvedSample sample)
{
    sample = (ClodResolvedSample)0;

    if (vis == 0xFFFFFFFFFFFFFFFF)
    {
        return false;
    }

    ConstantBuffer<PerFrameBuffer> perFrame = ResourceDescriptorHeap[0];
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera cam = cameras[perFrame.mainCameraIndex];

    float depth;
    uint clusterIndex;
    uint meshletTriangleIndex;
    UnpackVisKey(vis, depth, clusterIndex, meshletTriangleIndex);

    bool isReyesPatch = false;
    float3 patchDomain0 = float3(1.0f, 0.0f, 0.0f);
    float3 patchDomain1 = float3(0.0f, 1.0f, 0.0f);
    float3 patchDomain2 = float3(0.0f, 0.0f, 1.0f);
    float3 microTrianglePatchDomain0 = patchDomain0;
    float3 microTrianglePatchDomain1 = patchDomain1;
    float3 microTrianglePatchDomain2 = patchDomain2;
    uint reyesMicroTriangleIndex = meshletTriangleIndex;
    if (clusterIndex >= VISBUF_REYES_PATCH_INDEX_BASE && VISBUF_REYES_DICE_QUEUE_DESCRIPTOR_INDEX != 0xFFFFFFFFu)
    {
        StructuredBuffer<CLodReyesDiceQueueEntry> diceQueue = ResourceDescriptorHeap[VISBUF_REYES_DICE_QUEUE_DESCRIPTOR_INDEX];
        if (VISBUF_REYES_TESS_TABLE_CONFIGS_DESCRIPTOR_INDEX == 0xFFFFFFFFu ||
            VISBUF_REYES_TESS_TABLE_VERTICES_DESCRIPTOR_INDEX == 0xFFFFFFFFu ||
            VISBUF_REYES_TESS_TABLE_TRIANGLES_DESCRIPTOR_INDEX == 0xFFFFFFFFu)
        {
            return false;
        }

        StructuredBuffer<CLodReyesTessTableConfigEntry> tessTableConfigs = ResourceDescriptorHeap[VISBUF_REYES_TESS_TABLE_CONFIGS_DESCRIPTOR_INDEX];
        StructuredBuffer<uint> tessTableVertices = ResourceDescriptorHeap[VISBUF_REYES_TESS_TABLE_VERTICES_DESCRIPTOR_INDEX];
        StructuredBuffer<uint> tessTableTriangles = ResourceDescriptorHeap[VISBUF_REYES_TESS_TABLE_TRIANGLES_DESCRIPTOR_INDEX];
        CLodReyesDiceQueueEntry diceEntry = diceQueue[clusterIndex - VISBUF_REYES_PATCH_INDEX_BASE];
        clusterIndex = diceEntry.visibleClusterIndex;
        meshletTriangleIndex = diceEntry.sourcePrimitiveAndSplitConfig & 0xFFFFu;
        patchDomain0 = ReyesDecodeBarycentrics(diceEntry.domainVertex0Encoded);
        patchDomain1 = ReyesDecodeBarycentrics(diceEntry.domainVertex1Encoded);
        patchDomain2 = ReyesDecodeBarycentrics(diceEntry.domainVertex2Encoded);

        const uint microTriangleCount = ReyesGetDicePatchMicroTriangleCount(tessTableConfigs, diceEntry);
        if (reyesMicroTriangleIndex >= microTriangleCount)
        {
            return false;
        }

        ReyesDecodeMicroTrianglePatchDomain(
            tessTableConfigs,
            tessTableVertices,
            tessTableTriangles,
            reyesMicroTriangleIndex,
            diceEntry,
            microTrianglePatchDomain0,
            microTrianglePatchDomain1,
            microTrianglePatchDomain2);
        isReyesPatch = true;
    }

    MeshletResolveData md = LoadMeshletResolveData_Wave(clusterIndex);
    if (meshletTriangleIndex >= md.triangleCount)
    {
        return false;
    }

    uint3 triIdx = DecodeTriangleCompact(meshletTriangleIndex, md);

    float3 p0 = DecodeCompressedPosition(triIdx.x, md);
    float3 p1 = DecodeCompressedPosition(triIdx.y, md);
    float3 p2 = DecodeCompressedPosition(triIdx.z, md);
    float3 n0 = DecodeCompressedNormal(triIdx.x, md);
    float3 n1 = DecodeCompressedNormal(triIdx.y, md);
    float3 n2 = DecodeCompressedNormal(triIdx.z, md);
    float3 c0 = ((md.pageAttributeMask & CLOD_PAGE_ATTRIBUTE_COLOR) != 0u) ? DecodeCompressedColor(triIdx.x, md) : float3(1.0f, 1.0f, 1.0f);
    float3 c1 = ((md.pageAttributeMask & CLOD_PAGE_ATTRIBUTE_COLOR) != 0u) ? DecodeCompressedColor(triIdx.y, md) : float3(1.0f, 1.0f, 1.0f);
    float3 c2 = ((md.pageAttributeMask & CLOD_PAGE_ATTRIBUTE_COLOR) != 0u) ? DecodeCompressedColor(triIdx.z, md) : float3(1.0f, 1.0f, 1.0f);
    ApplyClodSkinning(triIdx.x, md, p0, n0);
    ApplyClodSkinning(triIdx.y, md, p1, n1);
    ApplyClodSkinning(triIdx.z, md, p2, n2);

    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    PerObjectBuffer obj = perObjectBuffer[md.objAndMesh.x];
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    MaterialInfo materialInfo = materialDataBuffer[md.materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;

    float4x4 viewProj = mul(cam.view, cam.projection);
    float4x4 objectToClip = mul(obj.model, viewProj);
    float4 clip0 = mul(float4(p0, 1.0f), objectToClip);
    float4 clip1 = mul(float4(p1, 1.0f), objectToClip);
    float4 clip2 = mul(float4(p2, 1.0f), objectToClip);
    float3 evalPos0 = p0;
    float3 evalPos1 = p1;
    float3 evalPos2 = p2;

    if (isReyesPatch)
    {
        const float3 sourcePatchBary0 = ReyesComposeSourceBarycentricsPoint(microTrianglePatchDomain0, patchDomain0, patchDomain1, patchDomain2);
        const float3 sourcePatchBary1 = ReyesComposeSourceBarycentricsPoint(microTrianglePatchDomain1, patchDomain0, patchDomain1, patchDomain2);
        const float3 sourcePatchBary2 = ReyesComposeSourceBarycentricsPoint(microTrianglePatchDomain2, patchDomain0, patchDomain1, patchDomain2);

        float3 patchPos0 = p0 * sourcePatchBary0.x + p1 * sourcePatchBary0.y + p2 * sourcePatchBary0.z;
        float3 patchPos1 = p0 * sourcePatchBary1.x + p1 * sourcePatchBary1.y + p2 * sourcePatchBary1.z;
        float3 patchPos2 = p0 * sourcePatchBary2.x + p1 * sourcePatchBary2.y + p2 * sourcePatchBary2.z;

        if (materialInfo.geometricDisplacementEnabled != 0u)
        {
            const float2 heightUv0 = DecodeCompressedUV(triIdx.x, materialInfo.heightUvSetIndex, md);
            const float2 heightUv1 = DecodeCompressedUV(triIdx.y, materialInfo.heightUvSetIndex, md);
            const float2 heightUv2 = DecodeCompressedUV(triIdx.z, materialInfo.heightUvSetIndex, md);
            const float3 patchNormal0 = normalize(n0 * sourcePatchBary0.x + n1 * sourcePatchBary0.y + n2 * sourcePatchBary0.z);
            const float3 patchNormal1 = normalize(n0 * sourcePatchBary1.x + n1 * sourcePatchBary1.y + n2 * sourcePatchBary1.z);
            const float3 patchNormal2 = normalize(n0 * sourcePatchBary2.x + n1 * sourcePatchBary2.y + n2 * sourcePatchBary2.z);
            const float2 patchUv0 = heightUv0 * sourcePatchBary0.x + heightUv1 * sourcePatchBary0.y + heightUv2 * sourcePatchBary0.z;
            const float2 patchUv1 = heightUv0 * sourcePatchBary1.x + heightUv1 * sourcePatchBary1.y + heightUv2 * sourcePatchBary1.z;
            const float2 patchUv2 = heightUv0 * sourcePatchBary2.x + heightUv1 * sourcePatchBary2.y + heightUv2 * sourcePatchBary2.z;
            patchPos0 = ReyesApplyGeometricDisplacement(materialInfo, patchPos0, patchNormal0, patchUv0);
            patchPos1 = ReyesApplyGeometricDisplacement(materialInfo, patchPos1, patchNormal1, patchUv1);
            patchPos2 = ReyesApplyGeometricDisplacement(materialInfo, patchPos2, patchNormal2, patchUv2);
        }

        evalPos0 = patchPos0;
        evalPos1 = patchPos1;
        evalPos2 = patchPos2;
        clip0 = mul(float4(evalPos0, 1.0f), objectToClip);
        clip1 = mul(float4(evalPos1, 1.0f), objectToClip);
        clip2 = mul(float4(evalPos2, 1.0f), objectToClip);
    }

    float2 winSize = float2(perFrame.screenResX, perFrame.screenResY);
    float2 pixelUv = (float2(pixel) + 0.5f) / winSize;
    float2 pixelNdc = float2(pixelUv.x * 2.0f - 1.0f, (1.0f - pixelUv.y) * 2.0f - 1.0f);

    BarycentricDeriv bary = CalcFullBary(clip0, clip1, clip2, pixelNdc, winSize);
    if (isReyesPatch)
    {
        const float3 sourcePatchBary0 = ReyesComposeSourceBarycentricsPoint(microTrianglePatchDomain0, patchDomain0, patchDomain1, patchDomain2);
        const float3 sourcePatchBary1 = ReyesComposeSourceBarycentricsPoint(microTrianglePatchDomain1, patchDomain0, patchDomain1, patchDomain2);
        const float3 sourcePatchBary2 = ReyesComposeSourceBarycentricsPoint(microTrianglePatchDomain2, patchDomain0, patchDomain1, patchDomain2);
        bary = ReyesComposeSourceBarycentrics(bary, sourcePatchBary0, sourcePatchBary1, sourcePatchBary2);
    }

    float3 interpPosX = InterpolateWithDeriv(bary, evalPos0.x, evalPos1.x, evalPos2.x);
    float3 interpPosY = InterpolateWithDeriv(bary, evalPos0.y, evalPos1.y, evalPos2.y);
    float3 interpPosZ = InterpolateWithDeriv(bary, evalPos0.z, evalPos1.z, evalPos2.z);

    float3 posOS = float3(interpPosX.x, interpPosY.x, interpPosZ.x);
    float3 dpdxOS = float3(interpPosX.y, interpPosY.y, interpPosZ.y);
    float3 dpdyOS = float3(interpPosX.z, interpPosY.z, interpPosZ.z);

    float3 worldPosition = mul(float4(posOS, 1.0f), obj.model).xyz;
    float3 positionVS = mul(float4(worldPosition, 1.0f), cam.view).xyz;

    float3x3 model3x3 = (float3x3)obj.model;
    float3 dpdx = mul(dpdxOS, model3x3);
    float3 dpdy = mul(dpdyOS, model3x3);

    float interpNX = InterpolateWithDeriv(bary, n0.x, n1.x, n2.x).x;
    float interpNY = InterpolateWithDeriv(bary, n0.y, n1.y, n2.y).x;
    float interpNZ = InterpolateWithDeriv(bary, n0.z, n1.z, n2.z).x;
    float3 normalOS = normalize(float3(interpNX, interpNY, interpNZ));
    if (isReyesPatch && materialInfo.geometricDisplacementEnabled != 0u)
    {
        const float3 geometricNormalOS = normalize(cross(evalPos1 - evalPos0, evalPos2 - evalPos0));
        if (all(isfinite(geometricNormalOS)) && dot(geometricNormalOS, normalOS) < 0.0f)
        {
            normalOS = -geometricNormalOS;
        }
        else if (all(isfinite(geometricNormalOS)))
        {
            normalOS = geometricNormalOS;
        }
    }
    float3 vertexColor = float3(
        InterpolateWithDeriv(bary, c0.x, c1.x, c2.x).x,
        InterpolateWithDeriv(bary, c0.y, c1.y, c2.y).x,
        InterpolateWithDeriv(bary, c0.z, c1.z, c2.z).x);

    StructuredBuffer<float4x4> normalMatrixBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::NormalMatrixBuffer)];
    float3x3 normalMatrix = (float3x3)normalMatrixBuffer[obj.normalMatrixBufferIndex];
    float3 worldNormal = normalize(mul(normalOS, normalMatrix));

    MaterialUvCache uvCache = BuildClodMaterialUvCache(materialInfo, materialFlags, md, triIdx, bary);
    MaterialUvBindings uvBindings = BuildMaterialUvBindings(materialInfo, materialFlags, uvCache);

    MaterialInputs materialInputs;
    SampleMaterialFromUvCacheRuntime(
        uvCache,
        uvBindings,
        worldNormal,
        worldPosition,
        vertexColor,
        materialInfo,
        materialFlags,
        dpdx,
        dpdy,
        materialInputs);

    sample.pixelCoords = pixel;
    sample.linearDepth = depth;
    sample.clusterIndex = clusterIndex;
    sample.meshletTriangleIndex = meshletTriangleIndex;
    sample.meshletIndex = md.drawcallAndMeshlet.y;
    sample.positionWS = worldPosition;
    sample.positionVS = positionVS;
    sample.normalWSBase = worldNormal;
    sample.normalOS = normalOS;
    sample.vertexColor = vertexColor;
    sample.dpdxWS = dpdx;
    sample.dpdyWS = dpdy;
    sample.motionVector = ComputeClodMotionVector(
        posOS,
        worldPosition,
        obj.prevModel,
        mul(cam.view, cam.unjitteredProjection),
        mul(cam.prevView, cam.prevUnjitteredProjection));
    sample.materialInfo = materialInfo;
    sample.materialFlags = materialFlags;
    sample.materialInputs = materialInputs;
    return true;
}

bool ResolveClodSampleFromVisKey(uint64_t vis, uint2 pixel, out ClodResolvedSample sample)
{
    return ResolveClodSampleFromVisKeyWithFace(vis, pixel, false, sample);
}

#endif // __CLOD_RESOLVE_COMMON_HLSLI__
