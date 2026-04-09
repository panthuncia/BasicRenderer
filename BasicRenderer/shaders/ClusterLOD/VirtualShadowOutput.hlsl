#include "include/cbuffers.hlsli"
#include "include/clodVirtualShadowClipmap.hlsli"
#include "include/visibleClusterPacking.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"
#include "include/waveIntrinsicsHelpers.hlsli"
#include "PerPassRootConstants/clodRasterizationRootConstants.h"

ClodViewRasterInfo WaveLoadClodViewRasterInfo(StructuredBuffer<ClodViewRasterInfo> buffer, uint viewID)
{
    const uint4 matchMask = WaveMatch(viewID);
    const uint leaderLane = WaveFirstLaneFromMask(matchMask);

    ClodViewRasterInfo result = (ClodViewRasterInfo)0;
    if (WaveGetLaneIndex() == leaderLane)
    {
        result = buffer[viewID];
    }

    result.visibilityUAVDescriptorIndex = WaveReadLaneAt(result.visibilityUAVDescriptorIndex, leaderLane);
    result.opaqueVisibilitySRVDescriptorIndex = WaveReadLaneAt(result.opaqueVisibilitySRVDescriptorIndex, leaderLane);
    result.deepVisibilityHeadPointerUAVDescriptorIndex = WaveReadLaneAt(result.deepVisibilityHeadPointerUAVDescriptorIndex, leaderLane);
    result.scissorMinX = WaveReadLaneAt(result.scissorMinX, leaderLane);
    result.scissorMinY = WaveReadLaneAt(result.scissorMinY, leaderLane);
    result.scissorMaxX = WaveReadLaneAt(result.scissorMaxX, leaderLane);
    result.scissorMaxY = WaveReadLaneAt(result.scissorMaxY, leaderLane);
    result.viewportScaleX = WaveReadLaneAt(result.viewportScaleX, leaderLane);
    result.viewportScaleY = WaveReadLaneAt(result.viewportScaleY, leaderLane);
    result.pad0 = WaveReadLaneAt(result.pad0, leaderLane);
    result.pad1 = WaveReadLaneAt(result.pad1, leaderLane);
    result.pad2 = WaveReadLaneAt(result.pad2, leaderLane);
    return result;
}

CLodVirtualShadowClipmapInfo WaveLoadVirtualShadowClipmapInfo(
    StructuredBuffer<CLodVirtualShadowClipmapInfo> buffer,
    uint clipmapIndex)
{
    const uint4 matchMask = WaveMatch(clipmapIndex);
    const uint leaderLane = WaveFirstLaneFromMask(matchMask);

    CLodVirtualShadowClipmapInfo result = (CLodVirtualShadowClipmapInfo)0;
    if (WaveGetLaneIndex() == leaderLane)
    {
        result = buffer[clipmapIndex];
    }

    result.worldOriginX = WaveReadLaneAt(result.worldOriginX, leaderLane);
    result.worldOriginY = WaveReadLaneAt(result.worldOriginY, leaderLane);
    result.worldOriginZ = WaveReadLaneAt(result.worldOriginZ, leaderLane);
    result.texelWorldSize = WaveReadLaneAt(result.texelWorldSize, leaderLane);
    result.pageOffsetX = WaveReadLaneAt(result.pageOffsetX, leaderLane);
    result.pageOffsetY = WaveReadLaneAt(result.pageOffsetY, leaderLane);
    result.pageTableLayer = WaveReadLaneAt(result.pageTableLayer, leaderLane);
    result.shadowCameraBufferIndex = WaveReadLaneAt(result.shadowCameraBufferIndex, leaderLane);
    result.clipLevel = WaveReadLaneAt(result.clipLevel, leaderLane);
    result.flags = WaveReadLaneAt(result.flags, leaderLane);
    result.clearOffsetX = WaveReadLaneAt(result.clearOffsetX, leaderLane);
    result.clearOffsetY = WaveReadLaneAt(result.clearOffsetY, leaderLane);
    result.directionalLodBias = WaveReadLaneAt(result.directionalLodBias, leaderLane);
    result.virtualResolution = WaveReadLaneAt(result.virtualResolution, leaderLane);
    result.pageTableResolution = WaveReadLaneAt(result.pageTableResolution, leaderLane);
    result.physicalAtlasPagesWide = WaveReadLaneAt(result.physicalAtlasPagesWide, leaderLane);
    result.physicalAtlasPagesHigh = WaveReadLaneAt(result.physicalAtlasPagesHigh, leaderLane);
    return result;
}

[shader("pixel")]
void VirtualShadowBufferPSMain(VisBufferPSInput input, bool isFrontFace : SV_IsFrontFace, uint primID : SV_PrimitiveID)
{
    (void)isFrontFace;
    (void)primID;

    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    const ClodViewRasterInfo viewRasterInfo = WaveLoadClodViewRasterInfo(viewRasterInfoBuffer, input.viewID);

    uint2 pixel = uint2(input.position.xy);
    if (pixel.x < viewRasterInfo.scissorMinX ||
        pixel.y < viewRasterInfo.scissorMinY ||
        pixel.x >= viewRasterInfo.scissorMaxX ||
        pixel.y >= viewRasterInfo.scissorMaxY)
    {
        return;
    }

#if defined(PSO_ALPHA_TEST)
    TestAlpha(input.texcoord, input.materialDataIndex);
#endif

    const uint shadowVsmPayload = input.shadowClipmapIndex;
    const uint clipmapIndex = CLodVisibleClusterShadowClipmapIndexFromPayload(shadowVsmPayload);
    if (clipmapIndex >= kCLodVirtualShadowClipmapCount)
    {
        return;
    }

    StructuredBuffer<CLodVirtualShadowClipmapInfo> clipmapInfos = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_CLIPMAP_INFO_DESCRIPTOR_INDEX];
    const CLodVirtualShadowClipmapInfo clipmapInfo = WaveLoadVirtualShadowClipmapInfo(clipmapInfos, clipmapIndex);
    if (!CLodVirtualShadowClipmapIsValid(clipmapInfo) || clipmapInfo.shadowCameraBufferIndex != input.viewID)
    {
        return;
    }

    const float virtualResolution = max((float)CLOD_RASTER_VIRTUAL_SHADOW_VIRTUAL_RESOLUTION, 1.0f);
    const float2 shadowUv = saturate((float2(pixel) + 0.5f) / virtualResolution);
    const uint2 virtualPageCoords = CLodVirtualShadowVirtualPageCoordsFromUv(shadowUv, clipmapInfo);

    if (CLodVisibleClusterHasVsmBlockDataFromPayload(shadowVsmPayload))
    {
        const uint2 blockCoord = CLodVisibleClusterVsmBlockCoordFromPayload(shadowVsmPayload);
        if (any(CLodVirtualShadowBlockCoordFromPageCoord(virtualPageCoords) != blockCoord))
        {
            return;
        }

        const uint packedActiveRect = CLodVisibleClusterVsmActiveRectFromPayload(shadowVsmPayload);
        const uint2 localPageCoord = virtualPageCoords - CLodVisibleClusterVsmBlockOriginPageCoordFromPayload(shadowVsmPayload);
        if (!CLodVirtualShadowBlockActiveRectContainsPage(packedActiveRect, localPageCoord))
        {
            return;
        }
    }

    const uint2 wrappedPageCoords = CLodVirtualShadowWrappedPageCoords(virtualPageCoords, clipmapInfo);

    RWTexture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_DESCRIPTOR_INDEX];
    const uint3 pageCoords = uint3(wrappedPageCoords, clipmapInfo.pageTableLayer);
    const uint pageEntry = pageTable[pageCoords];
    if ((pageEntry & (kCLodVirtualShadowAllocatedMask | kCLodVirtualShadowDirtyMask)) !=
        (kCLodVirtualShadowAllocatedMask | kCLodVirtualShadowDirtyMask))
    {
        return;
    }

    const uint physicalPageIndex = pageEntry & kCLodVirtualShadowPhysicalPageIndexMask;
    const uint2 virtualTexelCoords = CLodVirtualShadowVirtualTexelCoordsFromUv(shadowUv, clipmapInfo);
    const uint2 atlasPixel = CLodVirtualShadowPhysicalAtlasPixel(physicalPageIndex, virtualTexelCoords, clipmapInfo);

    RWTexture2D<uint> physicalPages = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PHYSICAL_PAGES_DESCRIPTOR_INDEX];
    InterlockedMin(physicalPages[atlasPixel], asuint(input.linearDepth));
    uint ignored = 0u;
    InterlockedOr(
        pageTable[pageCoords],
        kCLodVirtualShadowContentValidMask | kCLodVirtualShadowRerenderedThisFrameMask,
        ignored);
}
