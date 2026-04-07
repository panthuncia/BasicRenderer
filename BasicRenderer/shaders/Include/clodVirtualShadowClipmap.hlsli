#ifndef __CLOD_VIRTUAL_SHADOW_CLIPMAP_HLSLI__
#define __CLOD_VIRTUAL_SHADOW_CLIPMAP_HLSLI__

static const uint kCLodVirtualShadowClipmapValidFlag = 0x1u;
static const uint kCLodVirtualShadowClipmapInvalidateFlag = 0x2u;
static const uint kCLodVirtualShadowAllocatedMask = 0x80000000u;
static const uint kCLodVirtualShadowDirtyMask = 0x40000000u;
static const uint kCLodVirtualShadowContentValidMask = 0x20000000u;
static const uint kCLodVirtualShadowVisitedMask = 0x10000000u;
static const uint kCLodVirtualShadowPhysicalPageIndexMask = 0x0FFFFFFFu;
static const uint kCLodVirtualShadowPhysicalPageResidentFlag = 0x1u;
static const uint kCLodVirtualShadowDefaultClipmapCount = 6u;
static const uint kCLodVirtualShadowClipmapCount = 16u;
static const uint CLodVirtualShadowDefaultClipmapCount = kCLodVirtualShadowDefaultClipmapCount;
static const uint CLodVirtualShadowMaxSupportedClipmapCount = kCLodVirtualShadowClipmapCount;
static const uint kCLodVirtualShadowDefaultVirtualResolution = 4096u;
static const uint kCLodVirtualShadowMaxVirtualResolution = 16384u;
static const uint kCLodVirtualShadowPhysicalPageSize = 128u;
static const uint kCLodVirtualShadowDefaultPhysicalPagesPerAxis = 64u;
static const uint kCLodVirtualShadowMaxPhysicalPagesPerAxis = 96u;
static const uint kCLodVirtualShadowDefaultPageTableResolution =
    kCLodVirtualShadowDefaultVirtualResolution / kCLodVirtualShadowPhysicalPageSize;
static const uint kCLodVirtualShadowMaxPageTableResolution =
    kCLodVirtualShadowMaxVirtualResolution / kCLodVirtualShadowPhysicalPageSize;
static const uint kCLodVirtualShadowMovedInstanceBitCapacity = 1u << 20;
static const uint kCLodVirtualShadowMovedInstanceBitWordCount =
    (kCLodVirtualShadowMovedInstanceBitCapacity + 31u) / 32u;
static const uint kInvalidShadowCameraIndex = 0xFFFFFFFFu;
static const uint kCLodVirtualShadowMarkTileSize = 16u;

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
    uint virtualResolution;
    uint pageTableResolution;
    uint physicalPagesPerAxis;
    uint pad0;
};

struct CLodVirtualShadowMainCameraInfo
{
    float4 positionWorldSpace;
    row_major matrix viewInverse;
    row_major matrix projectionInverse;
};

struct CLodVirtualShadowCompactShadowCameraInfo
{
    row_major matrix view;
    row_major matrix projection;
    row_major matrix viewProjection;
    uint isOrtho;
    uint3 pad;
};

struct CLodVirtualShadowMarkClipmapData
{
    float texelWorldSize;
    uint pageOffsetX;
    uint pageOffsetY;
    uint pageTableLayer;
    uint flags;
    uint virtualResolution;
    uint pageTableResolution;
    uint physicalPagesPerAxis;
    float4 directionalPageViewRow;
    row_major matrix shadowViewProjection;
};

struct CLodVirtualShadowMarkTileWorkItem
{
    uint tileCoordX;
    uint tileCoordY;
    uint minDepthBits;
    uint maxDepthBits;
};

bool CLodVirtualShadowCompactCameraIsOrtho(CLodVirtualShadowCompactShadowCameraInfo cameraInfo)
{
    return cameraInfo.isOrtho != 0u;
}

bool CLodVirtualShadowMarkClipmapIsValid(CLodVirtualShadowMarkClipmapData clipmapData)
{
    return (clipmapData.flags & kCLodVirtualShadowClipmapValidFlag) != 0u;
}

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
    uint setupResetLightDirectionChanged;
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
    uint visitedPageTableEntries[kCLodVirtualShadowClipmapCount];
    uint visitedDirtyPageTableEntries[kCLodVirtualShadowClipmapCount];
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

uint2 CLodVirtualShadowVirtualPageCoordsFromUv(float2 shadowUv, uint pageTableResolution)
{
    const float2 clampedUv = saturate(shadowUv);
    return min(
        (uint2)(clampedUv * pageTableResolution),
        max(pageTableResolution, 1u) - 1u);
}

uint2 CLodVirtualShadowVirtualPageCoordsFromUv(float2 shadowUv, CLodVirtualShadowClipmapInfo clipmapInfo)
{
    return CLodVirtualShadowVirtualPageCoordsFromUv(shadowUv, clipmapInfo.pageTableResolution);
}

uint2 CLodVirtualShadowVirtualPageCoordsFromUv(float2 shadowUv, CLodVirtualShadowMarkClipmapData clipmapData)
{
    return CLodVirtualShadowVirtualPageCoordsFromUv(shadowUv, clipmapData.pageTableResolution);
}

uint2 CLodVirtualShadowWrappedPageCoords(
    uint2 virtualPageCoords,
    CLodVirtualShadowClipmapInfo clipmapInfo)
{
    return uint2(
        CLodVirtualShadowWrapPageCoord(virtualPageCoords.x, clipmapInfo.pageOffsetX, clipmapInfo.pageTableResolution),
        CLodVirtualShadowWrapPageCoord(virtualPageCoords.y, clipmapInfo.pageOffsetY, clipmapInfo.pageTableResolution));
}

uint2 CLodVirtualShadowWrappedPageCoords(
    uint2 virtualPageCoords,
    CLodVirtualShadowMarkClipmapData clipmapData)
{
    return uint2(
        CLodVirtualShadowWrapPageCoord(virtualPageCoords.x, clipmapData.pageOffsetX, clipmapData.pageTableResolution),
        CLodVirtualShadowWrapPageCoord(virtualPageCoords.y, clipmapData.pageOffsetY, clipmapData.pageTableResolution));
}

uint2 CLodVirtualShadowVirtualTexelCoordsFromUv(float2 shadowUv, uint virtualResolution)
{
    const float2 clampedUv = saturate(shadowUv);
    return min(
        (uint2)(clampedUv * virtualResolution),
        max(virtualResolution, 1u) - 1u);
}

uint2 CLodVirtualShadowVirtualTexelCoordsFromUv(float2 shadowUv, CLodVirtualShadowClipmapInfo clipmapInfo)
{
    return CLodVirtualShadowVirtualTexelCoordsFromUv(shadowUv, clipmapInfo.virtualResolution);
}

uint2 CLodVirtualShadowVirtualTexelCoordsFromUv(float2 shadowUv, CLodVirtualShadowMarkClipmapData clipmapData)
{
    return CLodVirtualShadowVirtualTexelCoordsFromUv(shadowUv, clipmapData.virtualResolution);
}

uint2 CLodVirtualShadowPhysicalAtlasPixel(uint physicalPageIndex, uint2 virtualTexelCoords, uint physicalPagesPerAxis)
{
    const uint atlasPageX = physicalPageIndex % physicalPagesPerAxis;
    const uint atlasPageY = physicalPageIndex / physicalPagesPerAxis;
    return uint2(
        atlasPageX * kCLodVirtualShadowPhysicalPageSize + (virtualTexelCoords.x % kCLodVirtualShadowPhysicalPageSize),
        atlasPageY * kCLodVirtualShadowPhysicalPageSize + (virtualTexelCoords.y % kCLodVirtualShadowPhysicalPageSize));
}

uint2 CLodVirtualShadowPhysicalAtlasPixel(uint physicalPageIndex, uint2 virtualTexelCoords, CLodVirtualShadowClipmapInfo clipmapInfo)
{
    return CLodVirtualShadowPhysicalAtlasPixel(physicalPageIndex, virtualTexelCoords, clipmapInfo.physicalPagesPerAxis);
}

uint2 CLodVirtualShadowPhysicalAtlasPixel(uint physicalPageIndex, uint2 virtualTexelCoords, CLodVirtualShadowMarkClipmapData clipmapData)
{
    return CLodVirtualShadowPhysicalAtlasPixel(physicalPageIndex, virtualTexelCoords, clipmapData.physicalPagesPerAxis);
}

uint CLodVirtualShadowSelectClipmapIndex(
    float3 positionWS,
    float3 cameraPositionWS,
    float clip0TexelWorldSize,
    uint pageTableResolution,
    uint virtualResolution,
    uint activeClipmapCount)
{
    if (activeClipmapCount == 0u)
    {
        return 0u;
    }

    const float pageCount = (float)pageTableResolution;
    const float scaleRatio = pageCount > 0.0f ? max((pageCount - 2.0f) / pageCount, 0.0f) : 1.0f;
    const float clip0FrustumScale = 0.5f * max(clip0TexelWorldSize, 1.0e-5f) * (float)virtualResolution;
    const float baseScale = max(clip0FrustumScale * scaleRatio, 1.0e-5f);
    const float distanceFromCamera = length(positionWS - cameraPositionWS);
    const float clipLevel = ceil(log2(max(distanceFromCamera / baseScale, 1.0f)));
    return min((uint)max(clipLevel, 0.0f), activeClipmapCount - 1u);
}

#endif