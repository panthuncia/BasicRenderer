#ifndef CLOD_PAGE_JOB_RASTER_SHARED_HLSLI
#define CLOD_PAGE_JOB_RASTER_SHARED_HLSLI

uint PJ_ReadPackedBits32(ByteAddressBuffer buf, uint startBit, uint bitCount)
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

float3 PJ_DecodeCompressedPosition(
    uint meshletLocalVertex,
    uint positionBitstreamBase,
    uint positionBitOffset,
    uint bitsX, uint bitsY, uint bitsZ,
    uint quantExp,
    int3 minQ,
    uint pagePoolSlabDescriptorIndex)
{
    uint bitsPerVertex = bitsX + bitsY + bitsZ;
    uint bitCursor = positionBitstreamBase * 8u + positionBitOffset + meshletLocalVertex * bitsPerVertex;
    ByteAddressBuffer slab = ResourceDescriptorHeap[pagePoolSlabDescriptorIndex];
    uint px = PJ_ReadPackedBits32(slab, bitCursor, bitsX); bitCursor += bitsX;
    uint py = PJ_ReadPackedBits32(slab, bitCursor, bitsY); bitCursor += bitsY;
    uint pz = PJ_ReadPackedBits32(slab, bitCursor, bitsZ);
    int3 q = int3(px, py, pz) + minQ;
    float invScale = 1.0f / float(1u << quantExp);
    return float3(q) * invScale;
}

uint3 PJ_DecodeTriangle(ByteAddressBuffer slab, uint triStreamBase, uint triByteOffset, uint triLocalIndex)
{
    uint triOffset = triStreamBase + triByteOffset + triLocalIndex * 3u;
    uint alignedOffset = (triOffset / 4u) * 4u;
    uint firstWord = slab.Load(alignedOffset);
    uint byteOff = triOffset % 4u;
    uint i0 = (firstWord >> (byteOff * 8u)) & 0xFFu;
    uint i1, i2;
    if (byteOff <= 1u) {
        i1 = (firstWord >> ((byteOff + 1u) * 8u)) & 0xFFu;
        i2 = (firstWord >> ((byteOff + 2u) * 8u)) & 0xFFu;
    } else if (byteOff == 2u) {
        i1 = (firstWord >> 24u) & 0xFFu;
        uint secondWord = slab.Load(alignedOffset + 4u);
        i2 = secondWord & 0xFFu;
    } else {
        uint secondWord = slab.Load(alignedOffset + 4u);
        i1 = secondWord & 0xFFu;
        i2 = (secondWord >> 8u) & 0xFFu;
    }
    return uint3(i0, i1, i2);
}

SkinningInfluences PJ_DecodePackedJoints(
    uint meshletLocalVertex,
    CLodPageHeader hdr,
    CLodMeshletDescriptor desc,
    uint pageByteOffset,
    uint pagePoolSlabDescriptorIndex)
{
    SkinningInfluences skinning;
    skinning.joints0 = uint4(0, 0, 0, 0);
    skinning.joints1 = uint4(0, 0, 0, 0);
    skinning.weights0 = float4(0, 0, 0, 0);
    skinning.weights1 = float4(0, 0, 0, 0);
    if ((hdr.attributeMask & CLOD_PAGE_ATTRIBUTE_JOINTS) == 0u) return skinning;
    ByteAddressBuffer slab = ResourceDescriptorHeap[pagePoolSlabDescriptorIndex];
    uint addr = pageByteOffset + hdr.jointArrayOffset + (desc.vertexAttributeOffset + meshletLocalVertex) * 32u;
    skinning.joints0 = LoadUint4(addr, slab);
    skinning.joints1 = LoadUint4(addr + 16u, slab);
    return skinning;
}

SkinningInfluences PJ_DecodePackedWeights(
    uint meshletLocalVertex,
    CLodPageHeader hdr,
    CLodMeshletDescriptor desc,
    uint pageByteOffset,
    uint pagePoolSlabDescriptorIndex,
    SkinningInfluences skinning)
{
    if ((hdr.attributeMask & CLOD_PAGE_ATTRIBUTE_WEIGHTS) == 0u) return skinning;
    ByteAddressBuffer slab = ResourceDescriptorHeap[pagePoolSlabDescriptorIndex];
    uint addr = pageByteOffset + hdr.weightArrayOffset + (desc.vertexAttributeOffset + meshletLocalVertex) * 32u;
    skinning.weights0 = LoadFloat4(addr, slab);
    skinning.weights1 = LoadFloat4(addr + 16u, slab);
    return skinning;
}

void PJ_ComputeScreenBounds(float2 screenPositions[SW_RASTER_MAX_VERTS], uint vertCount, out float2 ssMin, out float2 ssMax)
{
    ssMin = float2(1e30f, 1e30f);
    ssMax = float2(-1e30f, -1e30f);
    for (uint i = 0; i < vertCount; ++i)
    {
        ssMin = min(ssMin, screenPositions[i]);
        ssMax = max(ssMax, screenPositions[i]);
    }
}

void PJ_ComputeTileCoverage(
    float2 ssMin,
    float2 ssMax,
    CLodVirtualShadowClipmapInfo clipmapInfo,
    out uint2 minPageCoord,
    out uint2 maxPageCoord,
    out uint2 minTileCoord,
    out uint2 tileCount,
    out uint tileJobCount)
{
    const float vRes = (float)max(clipmapInfo.virtualResolution, 1u);
    const float2 clampedMin = clamp(ssMin, float2(0.0f, 0.0f), float2(vRes - 1.0f, vRes - 1.0f));
    const float2 clampedMax = clamp(ssMax, float2(0.0f, 0.0f), float2(vRes - 1.0f, vRes - 1.0f));

    minPageCoord = CLodVirtualShadowVirtualPageCoordsFromPixel(uint2(clampedMin), clipmapInfo);
    maxPageCoord = CLodVirtualShadowVirtualPageCoordsFromPixel(uint2(clampedMax), clipmapInfo);
    minTileCoord = PageJobTileCoordFromPageCoord(minPageCoord);
    const uint2 maxTileCoord = PageJobTileCoordFromPageCoord(maxPageCoord);
    tileCount = maxTileCoord - minTileCoord + 1u;
    tileJobCount = min(tileCount.x * tileCount.y, PAGEJOB_MAX_TILE_JOBS_PER_CLUSTER);
}

void PJ_ComputeTilePageBounds(
    uint tileJobIndex,
    uint2 minPageCoord,
    uint2 maxPageCoord,
    uint2 minTileCoord,
    uint2 tileCount,
    out uint2 tileMinPageCoord,
    out uint2 scanMinPageCoord,
    out uint2 tileMaxPageCoord)
{
    const uint tileOffsetX = tileJobIndex % tileCount.x;
    const uint tileOffsetY = tileJobIndex / tileCount.x;
    const uint2 tileCoord = minTileCoord + uint2(tileOffsetX, tileOffsetY);
    tileMinPageCoord = PageJobTileOriginFromTileCoord(tileCoord);
    tileMaxPageCoord = min(
        tileMinPageCoord + uint2(PAGEJOB_TILE_PAGES_X - 1u, PAGEJOB_TILE_PAGES_Y - 1u),
        maxPageCoord);
    scanMinPageCoord = max(tileMinPageCoord, minPageCoord);
}

#endif // CLOD_PAGE_JOB_RASTER_SHARED_HLSLI