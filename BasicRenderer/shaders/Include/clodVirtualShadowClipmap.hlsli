#ifndef __CLOD_VIRTUAL_SHADOW_CLIPMAP_HLSLI__
#define __CLOD_VIRTUAL_SHADOW_CLIPMAP_HLSLI__

static const uint kCLodVirtualShadowClipmapValidFlag = 0x1u;
static const uint kCLodVirtualShadowClipmapInvalidateFlag = 0x2u;
static const uint kCLodVirtualShadowAllocatedMask = 0x80000000u;
static const uint kCLodVirtualShadowDirtyMask = 0x40000000u;
static const uint kCLodVirtualShadowContentValidMask = 0x20000000u;
static const uint kCLodVirtualShadowPhysicalPageIndexMask = 0x1FFFFFFFu;
static const uint kCLodVirtualShadowPhysicalPageResidentFlag = 0x1u;
static const uint kCLodVirtualShadowClipmapCount = 6u;
static const uint kCLodVirtualShadowVirtualResolution = 4096u;
static const uint kCLodVirtualShadowPhysicalPageSize = 128u;
static const uint kCLodVirtualShadowPhysicalPagesPerAxis = 64u;
static const uint kCLodVirtualShadowPageTableResolution =
    kCLodVirtualShadowVirtualResolution / kCLodVirtualShadowPhysicalPageSize;
static const uint kCLodVirtualShadowMovedInstanceBitCapacity = 1u << 20;
static const uint kCLodVirtualShadowMovedInstanceBitWordCount =
    (kCLodVirtualShadowMovedInstanceBitCapacity + 31u) / 32u;
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
    int clearOffsetX;
    int clearOffsetY;
};

struct CLodVirtualShadowStats
{
    uint activeClipmapCount;
    uint validClipmapCount;
    uint allocationRequestCount;
    uint allocationDispatchGroupCount;
    uint freePhysicalPageCount;
    uint reusablePhysicalPageCount;
    uint setupResetApplied;
    uint markRequestOverflowCount;
    uint setupResetForced;
    uint setupResetNoPreviousState;
    uint setupResetStructureMismatch;
    uint pad0;
    uint setupWrappedClearedPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint setupStaleDirtyClearedPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint markResidentCleanHits[kCLodVirtualShadowClipmapCount];
    uint markResidentDirtyHits[kCLodVirtualShadowClipmapCount];
    uint preAllocateNonZeroPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint preAllocateDirtyPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint selectedPixels[kCLodVirtualShadowClipmapCount];
    uint projectionRejectedPixels[kCLodVirtualShadowClipmapCount];
    uint requestedPages[kCLodVirtualShadowClipmapCount];
    uint nonZeroPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint allocatedPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint dirtyPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint clearedUnwrittenDirtyPages[kCLodVirtualShadowClipmapCount];
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

uint CLodVirtualShadowSelectClipmapIndex(
    float3 positionWS,
    float3 cameraPositionWS,
    float clip0TexelWorldSize,
    uint activeClipmapCount)
{
    if (activeClipmapCount == 0u)
    {
        return 0u;
    }

    const float pageCount = (float)kCLodVirtualShadowPageTableResolution;
    const float scaleRatio = pageCount > 0.0f ? max((pageCount - 2.0f) / pageCount, 0.0f) : 1.0f;
    const float clip0FrustumScale = 0.5f * max(clip0TexelWorldSize, 1.0e-5f) * (float)kCLodVirtualShadowVirtualResolution;
    const float baseScale = max(clip0FrustumScale * scaleRatio, 1.0e-5f);
    const float distanceFromCamera = length(positionWS - cameraPositionWS);
    const float clipLevel = ceil(log2(max(distanceFromCamera / baseScale, 1.0f)));
    return min((uint)max(clipLevel, 0.0f), activeClipmapCount - 1u);
}

#endif