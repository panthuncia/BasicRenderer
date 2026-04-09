#include "include/cbuffers.hlsli"
#include "include/clodVirtualShadowClipmap.hlsli"
#include "include/materialFlags.hlsli"
#include "include/structs.hlsli"
#include "include/skinningCommon.hlsli"
#include "include/vertex.hlsli"
#include "PerPassRootConstants/clodWorkGraphRootConstants.h"
#include "PerPassRootConstants/clodRasterizationRootConstants.h"
#include "include/clodStructs.hlsli"
#include "include/clodPageAccess.hlsli"
#include "include/clodPageJobCommon.hlsli"
#include "include/visibleClusterPacking.hlsli"

struct CLodSoftwareRasterPageJobRecord
{
    uint sortedClusterIndex;
    uint physicalPageIndex;
    uint packedPagePixelOrigin;
    uint packedAtlasOrigin;
    uint clipmapLayer;
    uint wrappedPageX;
    uint wrappedPageY;
};

struct RasterizeClustersCommand
{
    uint baseClusterOffset;
    uint xDim;
    uint rasterBucketID;
    uint dispatchX;
    uint dispatchY;
    uint dispatchZ;
};

uint SWPJ_ReadPackedBits32(ByteAddressBuffer buf, uint startBit, uint bitCount)
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

float3 SWPJ_DecodeCompressedPosition(
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
    uint px = SWPJ_ReadPackedBits32(slab, bitCursor, bitsX); bitCursor += bitsX;
    uint py = SWPJ_ReadPackedBits32(slab, bitCursor, bitsY); bitCursor += bitsY;
    uint pz = SWPJ_ReadPackedBits32(slab, bitCursor, bitsZ);
    int3 q = int3(px, py, pz) + minQ;
    float invScale = 1.0f / float(1u << quantExp);
    return float3(q) * invScale;
}

uint3 SWPJ_DecodeTriangle(ByteAddressBuffer slab, uint triStreamBase, uint triByteOffset, uint triLocalIndex)
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

SkinningInfluences SWPJ_DecodePackedJoints(
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

SkinningInfluences SWPJ_DecodePackedWeights(
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

bool SWPJ_MaterialUsesAlphaTest(uint materialDataIndex)
{
    StructuredBuffer<MaterialInfo> materialDataBuffer =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];
    return (materialDataBuffer[materialDataIndex].materialFlags & MATERIAL_ALPHA_TEST) != 0u;
}

bool SWPJ_PageJobForceAll()
{
    return (CLOD_RASTER_PAGE_JOB_FLAGS & CLOD_WG_PAGE_JOB_FLAG_FORCE_ALL) != 0u;
}

float SWPJ_PageJobDiameterThreshold()
{
    return float((CLOD_RASTER_PAGE_JOB_FLAGS & CLOD_WG_PAGE_JOB_DIAMETER_THRESHOLD_MASK) >>
        CLOD_WG_PAGE_JOB_DIAMETER_THRESHOLD_SHIFT);
}

uint SWPJ_PageJobMaxPages()
{
    const uint v = (CLOD_RASTER_PAGE_JOB_FLAGS & CLOD_WG_PAGE_JOB_MAX_PAGES_MASK) >>
        CLOD_WG_PAGE_JOB_MAX_PAGES_SHIFT;
    return v == 0u ? PAGEJOB_MAX_TILE_JOBS_PER_CLUSTER : v;
}

groupshared float2 gs_expandScreenPos[SW_RASTER_MAX_VERTS];
groupshared uint gs_expandUse;
groupshared uint gs_expandMinPageX;
groupshared uint gs_expandMinPageY;
groupshared uint gs_expandMaxPageX;
groupshared uint gs_expandMaxPageY;
groupshared uint gs_expandDirtyCount;
groupshared uint gs_expandOverflow;
groupshared uint gs_expandBaseIndex;
groupshared uint gs_expandPackedPageCoords[PAGEJOB_MAX_TILE_JOBS_PER_CLUSTER];
groupshared uint gs_expandPhysicalPageIndices[PAGEJOB_MAX_TILE_JOBS_PER_CLUSTER];

groupshared float2 gs_pageRasterScreenPos[SW_RASTER_MAX_VERTS];
groupshared float gs_pageRasterLinearDepth[SW_RASTER_MAX_VERTS];

[shader("compute")]
[numthreads(SW_RASTER_THREADS, 1, 1)]
void SWPageJobExpandCSMain(uint3 dtid : SV_DispatchThreadID, uint GI : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    // StructuredBuffer<uint> histogram = ResourceDescriptorHeap[CLOD_RASTER_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX];
    // const uint bucketID = IndirectCommandSignatureRootConstant2;
    // const uint clusterCount = histogram[bucketID];

    // const uint linearizedGroupID = groupId.x + groupId.y * IndirectCommandSignatureRootConstant1;
    // if (linearizedGroupID >= clusterCount) {
    //     return;
    // }

    // ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)];
    // const uint sortedClusterIndex = IndirectCommandSignatureRootConstant0 + linearizedGroupID;
    // ByteAddressBuffer compactedVisibleClusters = ResourceDescriptorHeap[CLOD_RASTER_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX];
    // const uint4 packedCluster = CLodLoadVisibleClusterPacked(compactedVisibleClusters, sortedClusterIndex);

    // const uint viewID = CLodVisibleClusterViewID(packedCluster);
    // const uint instanceID = CLodVisibleClusterInstanceID(packedCluster);
    // const uint localMeshletIndex = CLodVisibleClusterLocalMeshletIndex(packedCluster);
    // const uint pageSlabDescriptorIndex = CLodVisibleClusterPageSlabDescriptorIndex(packedCluster);
    // const uint pageSlabByteOffset = CLodVisibleClusterPageSlabByteOffset(packedCluster);
    // const uint shadowClipmapIndex = CLodVisibleClusterShadowClipmapIndex(packedCluster);

    // StructuredBuffer<PerMeshInstanceBuffer> meshInstBuf =
    //     ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    // PerMeshInstanceBuffer meshInst = meshInstBuf[instanceID];
    // StructuredBuffer<PerMeshBuffer> perMeshBuffer =
    //     ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    // const uint materialDataIndex = perMeshBuffer[meshInst.perMeshBufferIndex].materialDataIndex;
    // StructuredBuffer<PerObjectBuffer> objBuf =
    //     ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    // PerObjectBuffer objData = objBuf[meshInst.perObjectBufferIndex];

    // StructuredBuffer<CLodVirtualShadowClipmapInfo> clipmapInfos =
    //     ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_CLIPMAP_INFO_DESCRIPTOR_INDEX];
    // CLodVirtualShadowClipmapInfo clipmapInfo = (CLodVirtualShadowClipmapInfo)0;
    // const bool hasClipmapInfo =
    //     shadowClipmapIndex < kCLodVirtualShadowClipmapCount &&
    //     CLodVirtualShadowClipmapIsValid(clipmapInfos[shadowClipmapIndex]) &&
    //     clipmapInfos[shadowClipmapIndex].shadowCameraBufferIndex == viewID;
    // if (hasClipmapInfo) {
    //     clipmapInfo = clipmapInfos[shadowClipmapIndex];
    // }

    // CLodPageHeader hdr = LoadPageHeader(pageSlabDescriptorIndex, pageSlabByteOffset);
    // CLodMeshletDescriptor desc = LoadMeshletDescriptor(
    //     pageSlabDescriptorIndex,
    //     pageSlabByteOffset,
    //     hdr.descriptorOffset,
    //     localMeshletIndex);
    // const uint vertCount = CLodDescVertexCount(desc);

    // StructuredBuffer<CullingCameraInfo> cullingCameras =
    //     ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
    // CullingCameraInfo cam = cullingCameras[viewID];
    // StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuf =
    //     ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    // ClodViewRasterInfo rasterInfo = viewRasterInfoBuf[viewID];

    // const float visWidth = float(rasterInfo.scissorMaxX - rasterInfo.scissorMinX);
    // const float visHeight = float(rasterInfo.scissorMaxY - rasterInfo.scissorMinY);
    // const float scissorMinXf = float(rasterInfo.scissorMinX);
    // const float scissorMinYf = float(rasterInfo.scissorMinY);
    // const uint positionBitstreamBase = pageSlabByteOffset + hdr.positionBitstreamOffset;
    // row_major matrix modelViewProjection = mul(objData.model, cam.viewProjection);

    // if (GI == 0) {
    //     gs_expandUse = 0u;
    //     gs_expandDirtyCount = 0u;
    //     gs_expandOverflow = 0u;
    //     gs_expandBaseIndex = 0u;
    // }

    // for (uint v = GI; v < vertCount; v += SW_RASTER_THREADS)
    // {
    //     float3 localPos = SWPJ_DecodeCompressedPosition(
    //         v,
    //         positionBitstreamBase,
    //         desc.positionBitOffset,
    //         CLodDescBitsX(desc), CLodDescBitsY(desc), CLodDescBitsZ(desc),
    //         hdr.compressedPositionQuantExp,
    //         int3(desc.minQx, desc.minQy, desc.minQz),
    //         pageSlabDescriptorIndex);
    //     if ((perMeshBuffer[meshInst.perMeshBufferIndex].vertexFlags & VERTEX_SKINNED) != 0u)
    //     {
    //         SkinningInfluences skinning = SWPJ_DecodePackedJoints(v, hdr, desc, pageSlabByteOffset, pageSlabDescriptorIndex);
    //         skinning = SWPJ_DecodePackedWeights(v, hdr, desc, pageSlabByteOffset, pageSlabDescriptorIndex, skinning);
    //         localPos = mul(float4(localPos, 1.0f), BuildSkinMatrix(meshInst.skinningInstanceSlot, skinning)).xyz;
    //     }

    //     float4 clipPos = mul(float4(localPos, 1.0f), modelViewProjection);
    //     float invW = 1.0f / clipPos.w;
    //     float2 ndc = clipPos.xy * invW;
    //     float2 screen;
    //     screen.x = (ndc.x + 1.0f) * 0.5f * visWidth + scissorMinXf;
    //     screen.y = (1.0f - ndc.y) * 0.5f * visHeight + scissorMinYf;
    //     gs_expandScreenPos[v] = screen;
    // }

    // GroupMemoryBarrierWithGroupSync();

    // if (GI == 0)
    // {
    //     if (hasClipmapInfo && !SWPJ_MaterialUsesAlphaTest(materialDataIndex))
    //     {
    //         float2 ssMin = float2(1e30f, 1e30f);
    //         float2 ssMax = float2(-1e30f, -1e30f);
    //         for (uint i = 0; i < vertCount; ++i)
    //         {
    //             ssMin = min(ssMin, gs_expandScreenPos[i]);
    //             ssMax = max(ssMax, gs_expandScreenPos[i]);
    //         }

    //         const float footprint = max(ssMax.x - ssMin.x, ssMax.y - ssMin.y);
    //         gs_expandUse = (SWPJ_PageJobForceAll() || footprint >= SWPJ_PageJobDiameterThreshold()) ? 1u : 0u;
    //         if (gs_expandUse != 0u)
    //         {
    //             const float vRes = float(max(clipmapInfo.virtualResolution, 1u));
    //             ssMin = clamp(ssMin, float2(0.0f, 0.0f), float2(vRes - 1.0f, vRes - 1.0f));
    //             ssMax = clamp(ssMax, float2(0.0f, 0.0f), float2(vRes - 1.0f, vRes - 1.0f));

    //             const uint2 minPageCoord = CLodVirtualShadowVirtualPageCoordsFromPixel(uint2(ssMin), clipmapInfo);
    //             const uint2 maxPageCoord = CLodVirtualShadowVirtualPageCoordsFromPixel(uint2(ssMax), clipmapInfo);
    //             gs_expandMinPageX = minPageCoord.x;
    //             gs_expandMinPageY = minPageCoord.y;
    //             gs_expandMaxPageX = maxPageCoord.x;
    //             gs_expandMaxPageY = maxPageCoord.y;
    //         }
    //     }
    // }

    // GroupMemoryBarrierWithGroupSync();

    // if (gs_expandUse == 0u)
    // {
    //     return;
    // }

    // RWTexture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_DESCRIPTOR_INDEX];
    // const uint2 minPageCoord = uint2(gs_expandMinPageX, gs_expandMinPageY);
    // const uint2 maxPageCoord = uint2(gs_expandMaxPageX, gs_expandMaxPageY);
    // const uint pageRangeWidth = maxPageCoord.x - minPageCoord.x + 1u;
    // const uint totalPageCount = pageRangeWidth * (maxPageCoord.y - minPageCoord.y + 1u);

    // for (uint pageIndex = GI; pageIndex < totalPageCount; pageIndex += SW_RASTER_THREADS)
    // {
    //     const uint pageX = minPageCoord.x + (pageIndex % pageRangeWidth);
    //     const uint pageY = minPageCoord.y + (pageIndex / pageRangeWidth);
    //     const uint2 wrappedCoords = CLodVirtualShadowWrappedPageCoords(uint2(pageX, pageY), clipmapInfo);
    //     const uint pageEntry = pageTable[uint3(wrappedCoords, clipmapInfo.pageTableLayer)];
    //     if (!CLodVirtualShadowPageEntryCanRaster(pageEntry))
    //     {
    //         continue;
    //     }

    //     uint slot = 0u;
    //     InterlockedAdd(gs_expandDirtyCount, 1u, slot);
    //     if (slot < PAGEJOB_MAX_TILE_JOBS_PER_CLUSTER)
    //     {
    //         gs_expandPackedPageCoords[slot] = (pageX & 0xFFFFu) | ((pageY & 0xFFFFu) << 16u);
    //         gs_expandPhysicalPageIndices[slot] = pageEntry & kCLodVirtualShadowPhysicalPageIndexMask;
    //     }
    //     else
    //     {
    //         InterlockedOr(gs_expandOverflow, 1u);
    //     }
    // }

    // GroupMemoryBarrierWithGroupSync();

    // const uint dirtyCount = gs_expandDirtyCount;
    // const bool commitPageJobs =
    //     dirtyCount != 0u &&
    //     dirtyCount <= SWPJ_PageJobMaxPages() &&
    //     gs_expandOverflow == 0u;

    // RWStructuredBuffer<uint> pageJobCount = ResourceDescriptorHeap[CLOD_RASTER_PAGE_JOB_COUNT_DESCRIPTOR_INDEX];
    // RWStructuredBuffer<uint> pageJobClusterTags = ResourceDescriptorHeap[CLOD_RASTER_PAGE_JOB_CLUSTER_TAGS_DESCRIPTOR_INDEX];

    // if (GI == 0 && commitPageJobs)
    // {
    //     InterlockedAdd(pageJobCount[0], dirtyCount, gs_expandBaseIndex);
    //     pageJobClusterTags[sortedClusterIndex] = perFrameBuffer.frameIndex;
    // }

    // GroupMemoryBarrierWithGroupSync();

    // if (!commitPageJobs || GI >= dirtyCount)
    // {
    //     return;
    // }

    // RWStructuredBuffer<CLodSoftwareRasterPageJobRecord> pageJobRecords = ResourceDescriptorHeap[CLOD_RASTER_PAGE_JOB_RECORDS_DESCRIPTOR_INDEX];
    // const uint packedPageCoords = gs_expandPackedPageCoords[GI];
    // const uint pageX = packedPageCoords & 0xFFFFu;
    // const uint pageY = (packedPageCoords >> 16u) & 0xFFFFu;
    // const uint physicalPageIndex = gs_expandPhysicalPageIndices[GI];
    // const uint physicalAtlasPagesWide = max(clipmapInfo.physicalAtlasPagesWide, 1u);

    // CLodSoftwareRasterPageJobRecord rec = (CLodSoftwareRasterPageJobRecord)0;
    // rec.sortedClusterIndex = sortedClusterIndex;
    // rec.physicalPageIndex = physicalPageIndex;
    // rec.packedPagePixelOrigin = ((pageX * kCLodVirtualShadowPhysicalPageSize) & 0xFFFFu) |
    //     (((pageY * kCLodVirtualShadowPhysicalPageSize) & 0xFFFFu) << 16u);
    // rec.packedAtlasOrigin = (((physicalPageIndex % physicalAtlasPagesWide) * kCLodVirtualShadowPhysicalPageSize) & 0xFFFFu) |
    //     ((((physicalPageIndex / physicalAtlasPagesWide) * kCLodVirtualShadowPhysicalPageSize) & 0xFFFFu) << 16u);
    // rec.clipmapLayer = clipmapInfo.pageTableLayer;
    // const uint2 wrappedCoords = CLodVirtualShadowWrappedPageCoords(uint2(pageX, pageY), clipmapInfo);
    // rec.wrappedPageX = wrappedCoords.x;
    // rec.wrappedPageY = wrappedCoords.y;
    // pageJobRecords[gs_expandBaseIndex + GI] = rec;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void SWPageJobBuildIndirectArgsCSMain(uint3 dtid : SV_DispatchThreadID)
{
    // StructuredBuffer<uint> pageJobCount = ResourceDescriptorHeap[CLOD_RASTER_PAGE_JOB_COUNT_DESCRIPTOR_INDEX];
    // RWStructuredBuffer<RasterizeClustersCommand> outArgs = ResourceDescriptorHeap[CLOD_RASTER_PAGE_JOB_INDIRECT_ARGS_DESCRIPTOR_INDEX];

    // RasterizeClustersCommand cmd = (RasterizeClustersCommand)0;
    // const uint count = pageJobCount[0];
    // if (count > 0u)
    // {
    //     const uint kMaxDim = 65535u;
    //     uint dispatchX = (uint)ceil(sqrt((float)count));
    //     dispatchX = min(dispatchX, kMaxDim);
    //     uint dispatchY = (count + dispatchX - 1u) / dispatchX;
    //     dispatchY = min(dispatchY, kMaxDim);

    //     cmd.baseClusterOffset = 0u;
    //     cmd.xDim = dispatchX;
    //     cmd.rasterBucketID = 0u;
    //     cmd.dispatchX = dispatchX;
    //     cmd.dispatchY = dispatchY;
    //     cmd.dispatchZ = 1u;
    // }
    // outArgs[0] = cmd;
}

[shader("compute")]
[numthreads(SW_RASTER_THREADS, 1, 1)]
void SWPageJobRasterPageCSMain(uint3 dtid : SV_DispatchThreadID, uint GI : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    // const uint linearizedGroupID = groupId.x + groupId.y * IndirectCommandSignatureRootConstant1;
    // const uint pageJobIndex = IndirectCommandSignatureRootConstant0 + linearizedGroupID;

    // StructuredBuffer<uint> pageJobCount = ResourceDescriptorHeap[CLOD_RASTER_PAGE_JOB_COUNT_DESCRIPTOR_INDEX];
    // if (pageJobIndex >= pageJobCount[0])
    // {
    //     return;
    // }

    // StructuredBuffer<CLodSoftwareRasterPageJobRecord> pageJobRecords = ResourceDescriptorHeap[CLOD_RASTER_PAGE_JOB_RECORDS_DESCRIPTOR_INDEX];
    // CLodSoftwareRasterPageJobRecord rec = pageJobRecords[pageJobIndex];
    // const uint sortedClusterIndex = rec.sortedClusterIndex;
    // ByteAddressBuffer compactedVisibleClusters = ResourceDescriptorHeap[CLOD_RASTER_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX];
    // const uint4 packedCluster = CLodLoadVisibleClusterPacked(compactedVisibleClusters, sortedClusterIndex);

    // const uint viewID = CLodVisibleClusterViewID(packedCluster);
    // const uint instanceID = CLodVisibleClusterInstanceID(packedCluster);
    // const uint localMeshletIndex = CLodVisibleClusterLocalMeshletIndex(packedCluster);
    // const uint pageSlabDescriptorIndex = CLodVisibleClusterPageSlabDescriptorIndex(packedCluster);
    // const uint pageSlabByteOffset = CLodVisibleClusterPageSlabByteOffset(packedCluster);

    // CLodPageHeader hdr = LoadPageHeader(pageSlabDescriptorIndex, pageSlabByteOffset);
    // CLodMeshletDescriptor desc = LoadMeshletDescriptor(
    //     pageSlabDescriptorIndex,
    //     pageSlabByteOffset,
    //     hdr.descriptorOffset,
    //     localMeshletIndex);
    // const uint vertCount = CLodDescVertexCount(desc);
    // const uint triCount = CLodDescTriangleCount(desc);

    // StructuredBuffer<PerMeshInstanceBuffer> meshInstBuf =
    //     ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    // PerMeshInstanceBuffer meshInst = meshInstBuf[instanceID];
    // StructuredBuffer<PerMeshBuffer> perMeshBuffer =
    //     ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    // StructuredBuffer<PerObjectBuffer> objBuf =
    //     ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    // PerObjectBuffer objData = objBuf[meshInst.perObjectBufferIndex];
    // StructuredBuffer<CullingCameraInfo> cullingCameras =
    //     ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
    // CullingCameraInfo cam = cullingCameras[viewID];
    // StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuf =
    //     ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    // ClodViewRasterInfo rasterInfo = viewRasterInfoBuf[viewID];

    // const float visWidth = float(rasterInfo.scissorMaxX - rasterInfo.scissorMinX);
    // const float visHeight = float(rasterInfo.scissorMaxY - rasterInfo.scissorMinY);
    // const float scissorMinXf = float(rasterInfo.scissorMinX);
    // const float scissorMinYf = float(rasterInfo.scissorMinY);
    // const uint positionBitstreamBase = pageSlabByteOffset + hdr.positionBitstreamOffset;
    // row_major matrix modelViewProjection = mul(objData.model, cam.viewProjection);
    // float4 modelViewZ = mul(objData.model, cam.viewZ);

    // for (uint v = GI; v < vertCount; v += SW_RASTER_THREADS)
    // {
    //     float3 localPos = SWPJ_DecodeCompressedPosition(
    //         v,
    //         positionBitstreamBase,
    //         desc.positionBitOffset,
    //         CLodDescBitsX(desc), CLodDescBitsY(desc), CLodDescBitsZ(desc),
    //         hdr.compressedPositionQuantExp,
    //         int3(desc.minQx, desc.minQy, desc.minQz),
    //         pageSlabDescriptorIndex);
    //     if ((perMeshBuffer[meshInst.perMeshBufferIndex].vertexFlags & VERTEX_SKINNED) != 0u)
    //     {
    //         SkinningInfluences skinning = SWPJ_DecodePackedJoints(v, hdr, desc, pageSlabByteOffset, pageSlabDescriptorIndex);
    //         skinning = SWPJ_DecodePackedWeights(v, hdr, desc, pageSlabByteOffset, pageSlabDescriptorIndex, skinning);
    //         localPos = mul(float4(localPos, 1.0f), BuildSkinMatrix(meshInst.skinningInstanceSlot, skinning)).xyz;
    //     }

    //     float4 localPos4 = float4(localPos, 1.0f);
    //     float4 clipPos = mul(localPos4, modelViewProjection);
    //     float viewZ = dot(localPos4, modelViewZ);
    //     float invW = 1.0f / clipPos.w;
    //     float2 ndc = clipPos.xy * invW;
    //     float2 screen;
    //     screen.x = (ndc.x + 1.0f) * 0.5f * visWidth + scissorMinXf;
    //     screen.y = (1.0f - ndc.y) * 0.5f * visHeight + scissorMinYf;
    //     gs_pageRasterScreenPos[v] = screen;
    //     gs_pageRasterLinearDepth[v] = -viewZ;
    // }

    // GroupMemoryBarrierWithGroupSync();

    // RWTexture2D<uint> physicalPages = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PHYSICAL_PAGES_DESCRIPTOR_INDEX];
    // RWTexture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_DESCRIPTOR_INDEX];
    // const bool reverseWinding = (objData.objectFlags & OBJECT_FLAG_REVERSE_WINDING) != 0u;
    // ByteAddressBuffer slab = ResourceDescriptorHeap[pageSlabDescriptorIndex];

    // const uint pagePixelMinX = rec.packedPagePixelOrigin & 0xFFFFu;
    // const uint pagePixelMinY = (rec.packedPagePixelOrigin >> 16u) & 0xFFFFu;
    // const uint atlasBaseX = rec.packedAtlasOrigin & 0xFFFFu;
    // const uint atlasBaseY = (rec.packedAtlasOrigin >> 16u) & 0xFFFFu;
    // const uint pagePixelMaxX = pagePixelMinX + kCLodVirtualShadowPhysicalPageSize - 1u;
    // const uint pagePixelMaxY = pagePixelMinY + kCLodVirtualShadowPhysicalPageSize - 1u;

    // bool anyPixelWritten = false;

    // for (uint t = GI; t < triCount; t += SW_RASTER_THREADS)
    // {
    //     uint3 tri = SWPJ_DecodeTriangle(slab, pageSlabByteOffset + hdr.triangleStreamOffset, desc.triangleByteOffset, t);
    //     if (reverseWinding) { uint tmp = tri.y; tri.y = tri.z; tri.z = tmp; }

    //     float2 s0 = gs_pageRasterScreenPos[tri.x];
    //     float2 s1 = gs_pageRasterScreenPos[tri.y];
    //     float2 s2 = gs_pageRasterScreenPos[tri.z];
    //     float depth0 = gs_pageRasterLinearDepth[tri.x];
    //     float depth1 = gs_pageRasterLinearDepth[tri.y];
    //     float depth2 = gs_pageRasterLinearDepth[tri.z];
    //     if (depth0 <= 0.0f || depth1 <= 0.0f || depth2 <= 0.0f) continue;

    //     float2 e01 = s1 - s0;
    //     float2 e02 = s2 - s0;
    //     float twiceArea = e01.x * e02.y - e01.y * e02.x;
    //     if (twiceArea >= 0.0f) continue;

    //     const float invTwiceArea = -1.0f / twiceArea;
    //     float2 bbMinF = min(min(s0, s1), s2);
    //     float2 bbMaxF = max(max(s0, s1), s2);
    //     int2 minPx = max(int2(floor(bbMinF)), int2(pagePixelMinX, pagePixelMinY));
    //     int2 maxPx = min(int2(floor(bbMaxF)), int2(pagePixelMaxX, pagePixelMaxY));
    //     if (minPx.x > maxPx.x || minPx.y > maxPx.y) continue;

    //     float2 origin = float2(float(minPx.x) + 0.5f, float(minPx.y) + 0.5f);
    //     float2 e12 = s2 - s1;
    //     float2 e20 = s0 - s2;
    //     float row_b0 = ((origin.x - s1.x) * e12.y - (origin.y - s1.y) * e12.x) * invTwiceArea;
    //     float row_b1 = ((origin.x - s2.x) * e20.y - (origin.y - s2.y) * e20.x) * invTwiceArea;
    //     float dx_b0 = e12.y * invTwiceArea;
    //     float dx_b1 = e20.y * invTwiceArea;
    //     float dy_b0 = -e12.x * invTwiceArea;
    //     float dy_b1 = -e20.x * invTwiceArea;
    //     float scanline_b0 = row_b0;
    //     float scanline_b1 = row_b1;

    //     for (int py = minPx.y; py <= maxPx.y; ++py)
    //     {
    //         float b0 = scanline_b0;
    //         float b1 = scanline_b1;
    //         for (int px = minPx.x; px <= maxPx.x; ++px)
    //         {
    //             const float b2 = 1.0f - b0 - b1;
    //             if (b0 >= 0.0f && b1 >= 0.0f && b2 >= 0.0f)
    //             {
    //                 const float depth = b0 * depth0 + b1 * depth1 + b2 * depth2;
    //                 const uint2 localPixel = uint2(px - pagePixelMinX, py - pagePixelMinY);
    //                 const uint2 atlasPixel = uint2(atlasBaseX + localPixel.x, atlasBaseY + localPixel.y);
    //                 InterlockedMin(physicalPages[atlasPixel], asuint(depth));
    //                 anyPixelWritten = true;
    //             }
    //             b0 += dx_b0;
    //             b1 += dx_b1;
    //         }
    //         scanline_b0 += dy_b0;
    //         scanline_b1 += dy_b1;
    //     }
    // }

    // if (anyPixelWritten)
    // {
    //     uint ignored = 0u;
    //     InterlockedOr(
    //         pageTable[uint3(uint2(rec.wrappedPageX, rec.wrappedPageY), rec.clipmapLayer)],
    //         kCLodVirtualShadowContentValidMask | kCLodVirtualShadowRerenderedThisFrameMask,
    //         ignored);
    // }
}