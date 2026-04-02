#ifndef __CLOD_VIRTUAL_SHADOW_CLIPMAP_HLSLI__
#define __CLOD_VIRTUAL_SHADOW_CLIPMAP_HLSLI__

static const uint kCLodVirtualShadowClipmapValidFlag = 0x1u;
static const uint kCLodVirtualShadowAllocatedMask = 0x80000000u;
static const uint kCLodVirtualShadowDirtyMask = 0x40000000u;
static const uint kCLodVirtualShadowPhysicalPageIndexMask = 0x3FFFFFFFu;
static const uint kCLodVirtualShadowClipmapCount = 6u;
static const uint kCLodVirtualShadowVirtualResolution = 4096u;
static const uint kCLodVirtualShadowPhysicalPageSize = 128u;
static const uint kCLodVirtualShadowPhysicalPagesPerAxis = 64u;
static const uint kCLodVirtualShadowPageTableResolution =
    kCLodVirtualShadowVirtualResolution / kCLodVirtualShadowPhysicalPageSize;
static const uint kInvalidShadowCameraIndex = 0xFFFFFFFFu;

struct CLodVirtualShadowClipmapInfo
{
    float worldOriginX;
    float worldOriginY;
    float worldOriginZ;
    float texelWorldSize;
    uint pageOffsetX;
    uint pageOffsetY;
    uint pageTableLayer;
    uint shadowCameraBufferIndex;
    uint clipLevel;
    uint flags;
    uint pad0;
    uint pad1;
};

struct CLodVirtualShadowStats
{
    uint activeClipmapCount;
    uint validClipmapCount;
    uint allocationRequestCount;
    uint allocationDispatchGroupCount;
    uint freePhysicalPageCount;
    uint reusablePhysicalPageCount;
    uint pad0;
    uint pad1;
    uint preAllocateNonZeroPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint preAllocateDirtyPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint selectedPixels[kCLodVirtualShadowClipmapCount];
    uint projectionRejectedPixels[kCLodVirtualShadowClipmapCount];
    uint requestedPages[kCLodVirtualShadowClipmapCount];
    uint nonZeroPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint allocatedPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint dirtyPageTableEntries[kCLodVirtualShadowClipmapCount];
};

bool CLodVirtualShadowClipmapIsValid(CLodVirtualShadowClipmapInfo clipmapInfo)
{
    return (clipmapInfo.flags & kCLodVirtualShadowClipmapValidFlag) != 0u &&
        clipmapInfo.shadowCameraBufferIndex != kInvalidShadowCameraIndex;
}

uint CLodVirtualShadowWrapPageCoord(uint coord, uint offset, uint pageTableResolution)
{
    const uint wrappedOffset = pageTableResolution > 0u ? (offset % pageTableResolution) : 0u;
    return pageTableResolution > 0u
        ? ((coord + pageTableResolution - wrappedOffset) % pageTableResolution)
        : 0u;
}

uint2 CLodVirtualShadowVirtualPageCoordsFromUv(float2 shadowUv)
{
    const float2 clampedUv = saturate(shadowUv);
    return min(
        (uint2)(clampedUv * kCLodVirtualShadowPageTableResolution),
        kCLodVirtualShadowPageTableResolution - 1u);
}

uint2 CLodVirtualShadowWrappedPageCoords(
    uint2 virtualPageCoords,
    CLodVirtualShadowClipmapInfo clipmapInfo)
{
    return uint2(
        CLodVirtualShadowWrapPageCoord(virtualPageCoords.x, clipmapInfo.pageOffsetX, kCLodVirtualShadowPageTableResolution),
        CLodVirtualShadowWrapPageCoord(virtualPageCoords.y, clipmapInfo.pageOffsetY, kCLodVirtualShadowPageTableResolution));
}

uint2 CLodVirtualShadowVirtualTexelCoordsFromUv(float2 shadowUv)
{
    const float2 clampedUv = saturate(shadowUv);
    return min(
        (uint2)(clampedUv * kCLodVirtualShadowVirtualResolution),
        kCLodVirtualShadowVirtualResolution - 1u);
}

uint2 CLodVirtualShadowPhysicalAtlasPixel(uint physicalPageIndex, uint2 virtualTexelCoords)
{
    const uint atlasPageX = physicalPageIndex % kCLodVirtualShadowPhysicalPagesPerAxis;
    const uint atlasPageY = physicalPageIndex / kCLodVirtualShadowPhysicalPagesPerAxis;
    return uint2(
        atlasPageX * kCLodVirtualShadowPhysicalPageSize + (virtualTexelCoords.x % kCLodVirtualShadowPhysicalPageSize),
        atlasPageY * kCLodVirtualShadowPhysicalPageSize + (virtualTexelCoords.y % kCLodVirtualShadowPhysicalPageSize));
}

#endif