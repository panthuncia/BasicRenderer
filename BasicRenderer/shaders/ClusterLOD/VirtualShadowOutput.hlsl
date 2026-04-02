#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"
#include "PerPassRootConstants/clodRasterizationRootConstants.h"

static const uint kCLodVirtualShadowClipmapValidFlag = 0x1u;
static const uint kCLodVirtualShadowAllocatedMask = 0x80000000u;
static const uint kCLodVirtualShadowDirtyMask = 0x40000000u;
static const uint kCLodVirtualShadowPhysicalPageIndexMask = 0x3FFFFFFFu;
static const uint kCLodVirtualShadowClipmapCount = 6u;
static const uint kCLodVirtualShadowVirtualResolution = 4096u;
static const uint kCLodVirtualShadowPhysicalPageSize = 128u;
static const uint kCLodVirtualShadowPhysicalPagesPerAxis = 64u;

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
    uint flags;
    uint pad0;
    uint pad1;
    uint pad2;
};

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
        if ((candidate.flags & kCLodVirtualShadowClipmapValidFlag) == 0u)
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
    const uint pageTableResolution = max(CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_RESOLUTION, 1u);
    const uint pageX = min((uint)(shadowUv.x * pageTableResolution), pageTableResolution - 1u);
    const uint pageY = min((uint)(shadowUv.y * pageTableResolution), pageTableResolution - 1u);

    Texture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PAGE_TABLE_DESCRIPTOR_INDEX];
    const uint pageEntry = pageTable.Load(int4(pageX, pageY, clipmapInfo.pageTableLayer, 0));
    if ((pageEntry & (kCLodVirtualShadowAllocatedMask | kCLodVirtualShadowDirtyMask)) !=
        (kCLodVirtualShadowAllocatedMask | kCLodVirtualShadowDirtyMask))
    {
        return;
    }

    const uint physicalPageIndex = pageEntry & kCLodVirtualShadowPhysicalPageIndexMask;
    const uint atlasPageX = physicalPageIndex % kCLodVirtualShadowPhysicalPagesPerAxis;
    const uint atlasPageY = physicalPageIndex / kCLodVirtualShadowPhysicalPagesPerAxis;
    const uint virtualTexelX = min((uint)(shadowUv.x * kCLodVirtualShadowVirtualResolution), kCLodVirtualShadowVirtualResolution - 1u);
    const uint virtualTexelY = min((uint)(shadowUv.y * kCLodVirtualShadowVirtualResolution), kCLodVirtualShadowVirtualResolution - 1u);
    const uint2 atlasPixel = uint2(
        atlasPageX * kCLodVirtualShadowPhysicalPageSize + (virtualTexelX % kCLodVirtualShadowPhysicalPageSize),
        atlasPageY * kCLodVirtualShadowPhysicalPageSize + (virtualTexelY % kCLodVirtualShadowPhysicalPageSize));

    RWTexture2D<uint> physicalPages = ResourceDescriptorHeap[CLOD_RASTER_VIRTUAL_SHADOW_PHYSICAL_PAGES_DESCRIPTOR_INDEX];
    const float depth = saturate(input.position.z / max(input.position.w, 1.0e-6f));
    InterlockedMin(physicalPages[atlasPixel], asuint(depth));
}