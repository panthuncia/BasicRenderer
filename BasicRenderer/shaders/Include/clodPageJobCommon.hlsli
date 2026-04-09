#ifndef CLOD_PAGE_JOB_COMMON_HLSLI
#define CLOD_PAGE_JOB_COMMON_HLSLI

// Shared helpers for page-job pipeline tile/page packing.
// Included by both softwareRaster.hlsl (compute path) and pageJobRaster.hlsl (WG path).

#include "clodStructs.hlsli"

uint2 PageJobTileCoordFromPageCoord(uint2 pageCoord)
{
    return uint2(pageCoord.x / PAGEJOB_TILE_PAGES_X, pageCoord.y / PAGEJOB_TILE_PAGES_Y);
}

uint2 PageJobTileOriginFromTileCoord(uint2 tileCoord)
{
    return uint2(tileCoord.x * PAGEJOB_TILE_PAGES_X, tileCoord.y * PAGEJOB_TILE_PAGES_Y);
}

uint PackPageJobTileAndClipmap(uint2 tileMinPageCoord, uint clipmapLayer)
{
    return (tileMinPageCoord.x & 0xFFu) |
        ((tileMinPageCoord.y & 0xFFu) << 8u) |
        ((clipmapLayer & 0x1Fu) << 16u);
}

uint PackPageJobPageBounds(uint2 minPageCoord, uint2 maxPageCoord)
{
    return (minPageCoord.x & 0xFFu) |
        ((minPageCoord.y & 0xFFu) << 8u) |
        ((maxPageCoord.x & 0xFFu) << 16u) |
        ((maxPageCoord.y & 0xFFu) << 24u);
}

uint2 UnpackPageJobTileMinPageCoord(uint packedTileAndClipmap)
{
    return uint2(packedTileAndClipmap & 0xFFu, (packedTileAndClipmap >> 8u) & 0xFFu);
}

uint UnpackPageJobClipmapLayer(uint packedTileAndClipmap)
{
    return (packedTileAndClipmap >> 16u) & 0x1Fu;
}

uint2 UnpackPageJobMinPageCoord(uint packedPageBounds)
{
    return uint2(packedPageBounds & 0xFFu, (packedPageBounds >> 8u) & 0xFFu);
}

uint2 UnpackPageJobMaxPageCoord(uint packedPageBounds)
{
    return uint2((packedPageBounds >> 16u) & 0xFFu, (packedPageBounds >> 24u) & 0xFFu);
}

#endif // CLOD_PAGE_JOB_COMMON_HLSLI
