#include "include/cbuffers.hlsli"
#include "include/clodVirtualShadowClipmap.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"
#include "PerPassRootConstants/clodRasterizationRootConstants.h"

[shader("pixel")]
void VirtualShadowBufferPSMain(VisBufferPSInput input, bool isFrontFace : SV_IsFrontFace, uint primID : SV_PrimitiveID)
{
    (void)isFrontFace;
    (void)primID;

    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    ClodViewRasterInfo viewRasterInfo = viewRasterInfoBuffer[input.viewID];

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

    StructuredBuffer<CLodVirtualShadowClipmapInfo> clipmapInfos = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_CLIPMAP_INFO_DESCRIPTOR_INDEX];

    uint clipmapIndex = 0xFFFFFFFFu;
    CLodVirtualShadowClipmapInfo clipmapInfo = (CLodVirtualShadowClipmapInfo)0;
    [unroll]
    for (uint candidateIndex = 0u; candidateIndex < kCLodVirtualShadowClipmapCount; ++candidateIndex)
    {
        const CLodVirtualShadowClipmapInfo candidate = clipmapInfos[candidateIndex];
        if (!CLodVirtualShadowClipmapIsValid(candidate))
        {
            continue;
        }

        if (candidate.shadowCameraBufferIndex == input.viewID)
        {
            clipmapIndex = candidateIndex;
            clipmapInfo = candidate;
            break;
        }
    }

    if (clipmapIndex == 0xFFFFFFFFu)
    {
        return;
    }

    const float virtualResolution = max((float)CLOD_RASTER_VIRTUAL_SHADOW_VIRTUAL_RESOLUTION, 1.0f);
    const float2 shadowUv = saturate((float2(pixel) + 0.5f) / virtualResolution);
    const uint2 virtualPageCoords = CLodVirtualShadowVirtualPageCoordsFromUv(shadowUv);
    const uint2 wrappedPageCoords = CLodVirtualShadowWrappedPageCoords(virtualPageCoords, clipmapInfo);

    Texture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_DESCRIPTOR_INDEX];
    const uint pageEntry = pageTable.Load(int4(wrappedPageCoords, clipmapInfo.pageTableLayer, 0));
    if ((pageEntry & (kCLodVirtualShadowAllocatedMask | kCLodVirtualShadowDirtyMask)) !=
        (kCLodVirtualShadowAllocatedMask | kCLodVirtualShadowDirtyMask))
    {
        return;
    }

    const uint physicalPageIndex = pageEntry & kCLodVirtualShadowPhysicalPageIndexMask;
    const uint2 virtualTexelCoords = CLodVirtualShadowVirtualTexelCoordsFromUv(shadowUv);
    const uint2 atlasPixel = CLodVirtualShadowPhysicalAtlasPixel(physicalPageIndex, virtualTexelCoords);

    RWTexture2D<uint> physicalPages = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PHYSICAL_PAGES_DESCRIPTOR_INDEX];
    InterlockedMin(physicalPages[atlasPixel], asuint(input.linearDepth));
}