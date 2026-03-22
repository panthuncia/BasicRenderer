// Software rasterizer work graph node for tiny CLod meshlets.
// Compile with DXC target: lib_6_8 (Shader Model 6.8)
//
// Broadcasting launch: receives SWRasterBatchRecord from ClusterCull nodes.
// Each record carries up to 8 cluster indices; each cluster gets 4 thread groups
// of 32 threads (128 threads per cluster). Vertices are decoded into per-group
// groupshared, then triangles are rasterized via edge functions + InterlockedMin
// to the per-view visibility buffer.

#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/vertex.hlsli"
#include "PerPassRootConstants/clodWorkGraphRootConstants.h"
#include "PerPassRootConstants/clodRasterizationRootConstants.h"
#include "Include/clodStructs.hlsli"
#include "Include/clodPageAccess.hlsli"
#include "Include/visibilityPacking.hlsli"
#include "Include/debugPayload.hlsli"

// Bit-packed position decode (mirrors mesh.hlsl / gbuffer.hlsl)

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

float3 SWDecodeCompressedPosition(
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
    uint px = ReadPackedBits32(slab, bitCursor, bitsX);             bitCursor += bitsX;
    uint py = ReadPackedBits32(slab, bitCursor, bitsY);             bitCursor += bitsY;
    uint pz = ReadPackedBits32(slab, bitCursor, bitsZ);

    int3 q = int3(px, py, pz) + minQ;
    float invScale = 1.0f / float(1u << quantExp);
    return float3(q) * invScale;
}

// Triangle index decode (mirrors meshletCommon.hlsli)

uint3 SWDecodeTriangle(ByteAddressBuffer slab, uint triStreamBase, uint triByteOffset, uint triLocalIndex)
{
    uint triOffset = triStreamBase + triByteOffset + triLocalIndex * 3u;
    uint alignedOffset = (triOffset / 4u) * 4u;
    uint firstWord = slab.Load(alignedOffset);
    uint byteOff = triOffset % 4u;

    uint i0 = (firstWord >> (byteOff * 8u)) & 0xFFu;
    uint i1, i2;

    if (byteOff <= 1u)
    {
        i1 = (firstWord >> ((byteOff + 1u) * 8u)) & 0xFFu;
        i2 = (firstWord >> ((byteOff + 2u) * 8u)) & 0xFFu;
    }
    else if (byteOff == 2u)
    {
        i1 = (firstWord >> 24u) & 0xFFu;
        uint secondWord = slab.Load(alignedOffset + 4u);
        i2 = secondWord & 0xFFu;
    }
    else // byteOff == 3
    {
        uint secondWord = slab.Load(alignedOffset + 4u);
        i1 = secondWord & 0xFFu;
        i2 = (secondWord >> 8u) & 0xFFu;
    }

    return uint3(i0, i1, i2);
}

groupshared float2  gs_screenPos[SW_RASTER_MAX_VERTS];
groupshared float   gs_linearDepth[SW_RASTER_MAX_VERTS];

void SWRasterCluster(
    VisibleCluster vc,
    uint unsortedClusterIndex,
    uint GI,
    uint subGroup,
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuf)
{
    // Load page header + meshlet descriptor
    CLodPageHeader hdr = LoadPageHeader(vc.pageSlabDescriptorIndex, vc.pageSlabByteOffset);
    CLodMeshletDescriptor desc = LoadMeshletDescriptor(
        vc.pageSlabDescriptorIndex, vc.pageSlabByteOffset,
        hdr.descriptorOffset, vc.localMeshletIndex);

    const uint vertCount = CLodDescVertexCount(desc);
    const uint triCount  = CLodDescTriangleCount(desc);

    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<PerMeshInstanceBuffer> meshInstBuf =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInst = meshInstBuf[vc.instanceID];

    StructuredBuffer<PerObjectBuffer> objBuf =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    PerObjectBuffer objData = objBuf[meshInst.perObjectBufferIndex];

    StructuredBuffer<CullingCameraInfo> cullingCameras =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CullingCameraBuffer)];
    CullingCameraInfo cam = cullingCameras[vc.viewID];

    ClodViewRasterInfo rasterInfo = viewRasterInfoBuf[vc.viewID];

    const float visWidth  = float(rasterInfo.scissorMaxX - rasterInfo.scissorMinX);
    const float visHeight = float(rasterInfo.scissorMaxY - rasterInfo.scissorMinY);
    const float scissorMinXf = float(rasterInfo.scissorMinX);
    const float scissorMinYf = float(rasterInfo.scissorMinY);

    const uint positionBitstreamBase = vc.pageSlabByteOffset + hdr.positionBitstreamOffset;
    row_major matrix modelViewProjection = mul(objData.model, cam.viewProjection);
    float4 modelViewZ = mul(objData.model, cam.viewZ);

    for (uint v = GI; v < vertCount; v += SW_RASTER_THREADS)
    {
        float3 localPos = SWDecodeCompressedPosition(
            v,
            positionBitstreamBase,
            desc.positionBitOffset,
            CLodDescBitsX(desc), CLodDescBitsY(desc), CLodDescBitsZ(desc),
            hdr.compressedPositionQuantExp,
            int3(desc.minQx, desc.minQy, desc.minQz),
            vc.pageSlabDescriptorIndex);

        float4 localPos4 = float4(localPos, 1.0f);
        float4 clipPos  = mul(localPos4, modelViewProjection);
        float viewZ = dot(localPos4, modelViewZ);

        float invW = 1.0f / clipPos.w;
        float2 ndc = clipPos.xy * invW;

        float2 screen;
        screen.x = (ndc.x + 1.0f) * 0.5f * visWidth  + scissorMinXf;
        screen.y = (1.0f - ndc.y) * 0.5f * visHeight + scissorMinYf;

        gs_screenPos[v] = screen;
        gs_linearDepth[v] = -viewZ;
    }

    GroupMemoryBarrierWithGroupSync();

    RWTexture2D<uint64_t> visBuffer =
        ResourceDescriptorHeap[NonUniformResourceIndex(rasterInfo.visibilityUAVDescriptorIndex)];
    RWTexture2D<uint2> debugVisTex =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::DebugVisualization)];
    uint2 visDims;
    visBuffer.GetDimensions(visDims.x, visDims.y);
    const bool swRasterDebugMode = perFrameBuffer.outputType == OUTPUT_SW_RASTER;

    const bool reverseWinding = (objData.objectFlags & OBJECT_FLAG_REVERSE_WINDING) != 0u;

    ByteAddressBuffer slab = ResourceDescriptorHeap[vc.pageSlabDescriptorIndex];

    uint globalThread = subGroup * SW_RASTER_THREADS + GI;
    uint totalThreads = SW_RASTER_GROUPS_PER_CLUSTER * SW_RASTER_THREADS;

    for (uint t = globalThread; t < triCount; t += totalThreads)
    {
        uint3 tri = SWDecodeTriangle(slab, vc.pageSlabByteOffset + hdr.triangleStreamOffset, desc.triangleByteOffset, t);
        if (reverseWinding) { uint tmp = tri.y; tri.y = tri.z; tri.z = tmp; }

        float2 s0 = gs_screenPos[tri.x];
        float2 s1 = gs_screenPos[tri.y];
        float2 s2 = gs_screenPos[tri.z];

        float depth0 = gs_linearDepth[tri.x];
        float depth1 = gs_linearDepth[tri.y];
        float depth2 = gs_linearDepth[tri.z];

        if (depth0 <= 0.0f || depth1 <= 0.0f || depth2 <= 0.0f) continue;

        float2 e01 = s1 - s0;
        float2 e02 = s2 - s0;
        float twiceArea = e01.x * e02.y - e01.y * e02.x;
        if (swRasterDebugMode)
        {
            if (abs(twiceArea) <= 1e-8f) continue;

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
        }
        else if (twiceArea >= 0.0f) continue;

        float invTwiceArea = -1.0f / twiceArea;

        float2 bbMinF = min(min(s0, s1), s2);
        float2 bbMaxF = max(max(s0, s1), s2);

        int2 minPx = int2(floor(bbMinF));
        int2 maxPx = int2(floor(bbMaxF));

        minPx = max(minPx, int2(rasterInfo.scissorMinX, rasterInfo.scissorMinY));
        maxPx = min(maxPx, int2(int(rasterInfo.scissorMaxX) - 1, int(rasterInfo.scissorMaxY) - 1));
        minPx = max(minPx, int2(0, 0));
        maxPx = min(maxPx, int2(int(visDims.x) - 1, int(visDims.y) - 1));
        if (minPx.x > maxPx.x || minPx.y > maxPx.y) continue;

        float2 origin = float2(float(minPx.x) + 0.5f, float(minPx.y) + 0.5f);

        float2 e12 = s2 - s1;
        float2 e20 = s0 - s2;

        float row_b0 = ((origin.x - s1.x) * e12.y - (origin.y - s1.y) * e12.x) * invTwiceArea;
        float row_b1 = ((origin.x - s2.x) * e20.y - (origin.y - s2.y) * e20.x) * invTwiceArea;
        float row_b2 = 1.0f - row_b0 - row_b1;

        float dx_b0 =  e12.y * invTwiceArea;
        float dx_b1 =  e20.y * invTwiceArea;
        float dy_b0 = -e12.x * invTwiceArea;
        float dy_b1 = -e20.x * invTwiceArea;

        float scanline_b0 = row_b0;
        float scanline_b1 = row_b1;

        for (int py = minPx.y; py <= maxPx.y; py++)
        {
            float b0 = scanline_b0;
            float b1 = scanline_b1;

            for (int px = minPx.x; px <= maxPx.x; px++)
            {
                float b2 = 1.0f - b0 - b1;

                if (b0 >= 0.0f && b1 >= 0.0f && b2 >= 0.0f)
                {
                    if (swRasterDebugMode)
                    {
                        WriteDebugPixel(debugVisTex, uint2(px, py), PackDebugUint(1u));
                    }

                    float depth = b0 * depth0 + b1 * depth1 + b2 * depth2;
                    uint64_t visKey = PackVisKey(depth, unsortedClusterIndex, t);
                    InterlockedMin(visBuffer[uint2(px, py)], visKey);
                }

                b0 += dx_b0;
                b1 += dx_b1;
            }

            scanline_b0 += dy_b0;
            scanline_b1 += dy_b1;
        }
    }
}


// SWRaster: broadcasting work graph node for WG dispatch
[Shader("node")]
[NodeID("SWRaster")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(32, 1, 1)]
[NumThreads(SW_RASTER_THREADS, 1, 1)]
void WG_SWRaster(
    DispatchNodeInputRecord<SWRasterBatchRecord> inputRecord,
    uint GI : SV_GroupIndex,
    uint3 groupId : SV_GroupID)
{
    SWRasterBatchRecord batch = inputRecord.Get();

    // Each cluster gets SW_RASTER_GROUPS_PER_CLUSTER thread groups.
    uint clusterIdx = groupId.x / SW_RASTER_GROUPS_PER_CLUSTER;
    uint subGroup   = groupId.x % SW_RASTER_GROUPS_PER_CLUSTER;

    if (clusterIdx >= batch.numClusters) return;

    // Load VisibleCluster from buffer via indirection
    uint unsortedClusterIndex = batch.clusterIndices[clusterIdx];
    globallycoherent RWStructuredBuffer<VisibleCluster> visibleClusters =
        ResourceDescriptorHeap[CLOD_WG_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
    VisibleCluster vc = visibleClusters[unsortedClusterIndex];

    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuf =
        ResourceDescriptorHeap[CLOD_WG_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    SWRasterCluster(vc, unsortedClusterIndex, GI, subGroup, viewRasterInfoBuf);
}

// Non-WG SW raster
[shader("compute")]
[numthreads(SW_RASTER_THREADS, 1, 1)]
void SWRasterIndirectCSMain(uint3 dtid : SV_DispatchThreadID, uint GI : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    StructuredBuffer<uint> histogram = ResourceDescriptorHeap[CLOD_RASTER_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX];
    const uint bucketID = IndirectCommandSignatureRootConstant2;
    const uint clusterCount = histogram[bucketID];

    const uint linearizedGroupID = groupId.x + groupId.y * IndirectCommandSignatureRootConstant1;
    const uint clusterIdx = linearizedGroupID / SW_RASTER_GROUPS_PER_CLUSTER;
    const uint subGroup = linearizedGroupID % SW_RASTER_GROUPS_PER_CLUSTER;
    if (clusterIdx >= clusterCount) {
        return;
    }

    const uint sortedClusterIndex = IndirectCommandSignatureRootConstant0 + clusterIdx;
    StructuredBuffer<VisibleCluster> compactedVisibleClusters =
        ResourceDescriptorHeap[CLOD_RASTER_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> sortedToUnsortedMapping =
        ResourceDescriptorHeap[CLOD_RASTER_SORTED_TO_UNSORTED_MAPPING_DESCRIPTOR_INDEX];
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuf =
        ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];

    const VisibleCluster vc = compactedVisibleClusters[sortedClusterIndex];
    const uint unsortedClusterIndex = sortedToUnsortedMapping[sortedClusterIndex];
    SWRasterCluster(vc, unsortedClusterIndex, GI, subGroup, viewRasterInfoBuf);
}
