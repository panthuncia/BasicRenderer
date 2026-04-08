#ifndef CLOD_VISIBLE_CLUSTER_PACKING_HLSLI
#define CLOD_VISIBLE_CLUSTER_PACKING_HLSLI

static const uint CLOD_PACKED_VISIBLE_CLUSTER_STRIDE = 16u;
static const uint CLOD_PACKED_VISIBLE_CLUSTER_PAGE_SHIFT = 18u;
static const uint CLOD_PACKED_VISIBLE_CLUSTER_PAGE_MASK = 0x3FFu;
static const uint CLOD_PACKED_VISIBLE_CLUSTER_INVALID_SHADOW_CLIPMAP_INDEX = 0xFFFFFFFFu;

uint4 CLodLoadVisibleClusterPacked(ByteAddressBuffer buffer, uint clusterIndex)
{
    const uint byteOffset = clusterIndex * CLOD_PACKED_VISIBLE_CLUSTER_STRIDE;
    return uint4(
        buffer.Load(byteOffset + 0u),
        buffer.Load(byteOffset + 4u),
        buffer.Load(byteOffset + 8u),
        buffer.Load(byteOffset + 12u));
}

uint4 CLodLoadVisibleClusterPackedRW(RWByteAddressBuffer buffer, uint clusterIndex)
{
    const uint byteOffset = clusterIndex * CLOD_PACKED_VISIBLE_CLUSTER_STRIDE;
    return uint4(
        buffer.Load(byteOffset + 0u),
        buffer.Load(byteOffset + 4u),
        buffer.Load(byteOffset + 8u),
        buffer.Load(byteOffset + 12u));
}

uint4 CLodLoadVisibleClusterPackedGloballyCoherent(globallycoherent RWByteAddressBuffer buffer, uint clusterIndex)
{
    const uint byteOffset = clusterIndex * CLOD_PACKED_VISIBLE_CLUSTER_STRIDE;
    return uint4(
        buffer.Load(byteOffset + 0u),
        buffer.Load(byteOffset + 4u),
        buffer.Load(byteOffset + 8u),
        buffer.Load(byteOffset + 12u));
}

void CLodStoreVisibleClusterPackedWordsRW(RWByteAddressBuffer buffer, uint clusterIndex, uint4 packedCluster)
{
    const uint byteOffset = clusterIndex * CLOD_PACKED_VISIBLE_CLUSTER_STRIDE;
    buffer.Store(byteOffset + 0u, packedCluster.x);
    buffer.Store(byteOffset + 4u, packedCluster.y);
    buffer.Store(byteOffset + 8u, packedCluster.z);
    buffer.Store(byteOffset + 12u, packedCluster.w);
}

void CLodStoreVisibleClusterPackedWordsGloballyCoherent(globallycoherent RWByteAddressBuffer buffer, uint clusterIndex, uint4 packedCluster)
{
    const uint byteOffset = clusterIndex * CLOD_PACKED_VISIBLE_CLUSTER_STRIDE;
    buffer.Store(byteOffset + 0u, packedCluster.x);
    buffer.Store(byteOffset + 4u, packedCluster.y);
    buffer.Store(byteOffset + 8u, packedCluster.z);
    buffer.Store(byteOffset + 12u, packedCluster.w);
}

uint CLodVisibleClusterViewID(uint4 packedCluster)
{
    return packedCluster.x & 0xFFu;
}

uint CLodVisibleClusterInstanceID(uint4 packedCluster)
{
    return (packedCluster.x >> 8u) & 0xFFFFFFu;
}

uint CLodVisibleClusterLocalMeshletIndex(uint4 packedCluster)
{
    return packedCluster.y & 0x3FFFu;
}

uint CLodVisibleClusterGroupID(uint4 packedCluster)
{
    return ((packedCluster.y >> 14u) & 0x3FFFFu) | ((packedCluster.z & 0x3u) << 18u);
}

uint CLodVisibleClusterPageSlabDescriptorIndex(uint4 packedCluster)
{
    return (packedCluster.z >> 2u) & 0xFFFFFu;
}

uint CLodVisibleClusterPageSlabByteOffset(uint4 packedCluster)
{
    return ((packedCluster.z >> 22u) & CLOD_PACKED_VISIBLE_CLUSTER_PAGE_MASK) << CLOD_PACKED_VISIBLE_CLUSTER_PAGE_SHIFT;
}

uint CLodVisibleClusterShadowClipmapIndex(uint4 packedCluster)
{
    return packedCluster.w;
}

uint4 CLodPackVisibleCluster(
    uint viewID,
    uint instanceID,
    uint localMeshletIndex,
    uint groupID,
    uint pageSlabDescriptorIndex,
    uint pageSlabByteOffset,
    uint shadowClipmapIndex)
{
    const uint pageIndex = pageSlabByteOffset >> CLOD_PACKED_VISIBLE_CLUSTER_PAGE_SHIFT;
    return uint4(
        (viewID & 0xFFu) | ((instanceID & 0xFFFFFFu) << 8u),
        (localMeshletIndex & 0x3FFFu) | ((groupID & 0x3FFFFu) << 14u),
        ((groupID >> 18u) & 0x3u) | ((pageSlabDescriptorIndex & 0xFFFFFu) << 2u) | ((pageIndex & CLOD_PACKED_VISIBLE_CLUSTER_PAGE_MASK) << 22u),
        shadowClipmapIndex);
}

void CLodStoreVisibleClusterRW(
    RWByteAddressBuffer buffer,
    uint clusterIndex,
    uint viewID,
    uint instanceID,
    uint localMeshletIndex,
    uint groupID,
    uint pageSlabDescriptorIndex,
    uint pageSlabByteOffset,
    uint shadowClipmapIndex)
{
    CLodStoreVisibleClusterPackedWordsRW(
        buffer,
        clusterIndex,
        CLodPackVisibleCluster(
            viewID,
            instanceID,
            localMeshletIndex,
            groupID,
            pageSlabDescriptorIndex,
            pageSlabByteOffset,
            shadowClipmapIndex));
}

void CLodStoreVisibleClusterGloballyCoherent(
    globallycoherent RWByteAddressBuffer buffer,
    uint clusterIndex,
    uint viewID,
    uint instanceID,
    uint localMeshletIndex,
    uint groupID,
    uint pageSlabDescriptorIndex,
    uint pageSlabByteOffset,
    uint shadowClipmapIndex)
{
    CLodStoreVisibleClusterPackedWordsGloballyCoherent(
        buffer,
        clusterIndex,
        CLodPackVisibleCluster(
            viewID,
            instanceID,
            localMeshletIndex,
            groupID,
            pageSlabDescriptorIndex,
            pageSlabByteOffset,
            shadowClipmapIndex));
}

#endif
