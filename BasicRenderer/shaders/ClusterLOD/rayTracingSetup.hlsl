#include "include/cbuffers.hlsli"
#include "include/clodPageAccess.hlsli"
#include "PerPassRootConstants/clodRayTracingSetupRootConstants.h"

#define CLOD_RT_CLUSTER_INDEX_FORMAT_UINT8 1u
#define CLOD_RT_CLUSTER_GEOMETRY_FLAG_OPAQUE (1u << 2u)

struct CLodRtPageSource
{
    uint64_t slabDeviceAddress;
    uint64_t slabByteOffset;
    uint slabDescriptorIndex;
    uint firstMeshletInPage;
    uint meshletCount;
    uint outputClusterBase;
};

struct PackedRayTracingClusterBuildTriangleInfo
{
    uint clusterID;
    uint clusterFlags;
    uint countsAndFormats;
    uint baseGeometryIndexAndFlags;
    uint indexAndVertexBufferStride;
    uint geometryAndOpacityStride;
    uint64_t indexBuffer;
    uint64_t vertexBuffer;
    uint64_t geometryIndexAndFlagsBuffer;
    uint64_t opacityMicromapArray;
    uint64_t opacityMicromapIndexBuffer;
};

uint PackCountsAndFormats(uint triangleCount, uint vertexCount)
{
    return (triangleCount & 0x1FFu)
        | ((vertexCount & 0x1FFu) << 9u)
        | (CLOD_RT_CLUSTER_INDEX_FORMAT_UINT8 << 24u);
}

uint PackGeometryIndexAndFlags(uint geometryIndex, uint flags)
{
    return (geometryIndex & 0x00FFFFFFu) | ((flags & 0x7u) << 29u);
}

[numthreads(64, 1, 1)]
void CLodRayTracingSetupCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint pageSourceIndex = dispatchThreadId.x;
    if (pageSourceIndex >= CLOD_RT_SETUP_PAGE_SOURCE_COUNT)
    {
        return;
    }

    StructuredBuffer<CLodRtPageSource> pageSources =
        ResourceDescriptorHeap[CLOD_RT_SETUP_PAGE_SOURCES_DESCRIPTOR_INDEX];
    RWStructuredBuffer<PackedRayTracingClusterBuildTriangleInfo> buildInfos =
        ResourceDescriptorHeap[CLOD_RT_SETUP_BUILD_INFOS_DESCRIPTOR_INDEX];

    CLodRtPageSource source = pageSources[pageSourceIndex];
    uint pageBase = (uint)source.slabByteOffset;

    CLodPageHeader header = LoadPageHeader(source.slabDescriptorIndex, pageBase);
    uint meshletEnd = min(source.firstMeshletInPage + source.meshletCount, header.meshletCount);

    for (uint meshletIndex = source.firstMeshletInPage; meshletIndex < meshletEnd; ++meshletIndex)
    {
        uint outputIndex = source.outputClusterBase + (meshletIndex - source.firstMeshletInPage);
        if (outputIndex >= CLOD_RT_SETUP_BUILD_CLUSTER_CAPACITY)
        {
            break;
        }

        CLodMeshletDescriptor descriptor = LoadMeshletDescriptor(
            source.slabDescriptorIndex,
            pageBase,
            header.descriptorOffset,
            meshletIndex);

        uint vertexCount = (descriptor.bitsAndVertexCount >> 24u) & 0xFFu;
        uint triangleCount = descriptor.triangleCountAndRefinedGroup & 0xFFFFu;

        PackedRayTracingClusterBuildTriangleInfo buildInfo;
        if (header.compressedPositionQuantExp != CLOD_POSITION_FORMAT_FLOAT3)
        {
            buildInfo = (PackedRayTracingClusterBuildTriangleInfo)0;
            buildInfos[outputIndex] = buildInfo;
            continue;
        }

        buildInfo.clusterID = outputIndex;
        buildInfo.clusterFlags = 0u;
        buildInfo.countsAndFormats = PackCountsAndFormats(triangleCount, vertexCount);
        buildInfo.baseGeometryIndexAndFlags = PackGeometryIndexAndFlags(0u, CLOD_RT_CLUSTER_GEOMETRY_FLAG_OPAQUE);
        buildInfo.indexAndVertexBufferStride = 1u | (CLOD_POSITION_FORMAT_FLOAT3_STRIDE_BYTES << 16u);
        buildInfo.geometryAndOpacityStride = 0u;
        buildInfo.indexBuffer = source.slabDeviceAddress + source.slabByteOffset + header.triangleStreamOffset + descriptor.triangleByteOffset;
        buildInfo.vertexBuffer = source.slabDeviceAddress + source.slabByteOffset + header.positionBitstreamOffset + descriptor.positionBitOffset;
        buildInfo.geometryIndexAndFlagsBuffer = 0ull;
        buildInfo.opacityMicromapArray = 0ull;
        buildInfo.opacityMicromapIndexBuffer = 0ull;

        buildInfos[outputIndex] = buildInfo;
    }
}
