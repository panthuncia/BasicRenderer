#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"
#include "PerPassRootConstants/clodRasterizationRootConstants.h"
#include "include/visibilityPacking.hlsli"

[shader("pixel")]
void DeepVisibilityBufferPSMain(VisBufferPSInput input, bool isFrontFace : SV_IsFrontFace, uint primID : SV_PrimitiveID)
{
    StructuredBuffer<ClodViewRasterInfo> viewRasterInfoBuffer = ResourceDescriptorHeap[CLOD_RASTER_VIEW_RASTER_INFO_BUFFER_DESCRIPTOR_INDEX];
    ClodViewRasterInfo viewRasterInfo = viewRasterInfoBuffer[input.viewID];

    if (viewRasterInfo.opaqueVisibilitySRVDescriptorIndex == 0xFFFFFFFFu ||
        viewRasterInfo.deepVisibilityHeadPointerUAVDescriptorIndex == 0xFFFFFFFFu)
    {
        return;
    }

    uint2 pixel = input.position.xy;

    if (pixel.x < viewRasterInfo.scissorMinX ||
        pixel.y < viewRasterInfo.scissorMinY ||
        pixel.x >= viewRasterInfo.scissorMaxX ||
        pixel.y >= viewRasterInfo.scissorMaxY)
    {
        return;
    }

    Texture2D<uint64_t> opaqueVisibility =
        ResourceDescriptorHeap[NonUniformResourceIndex(viewRasterInfo.opaqueVisibilitySRVDescriptorIndex)];
    RWTexture2D<uint> deepVisibilityHeadPointers =
        ResourceDescriptorHeap[NonUniformResourceIndex(viewRasterInfo.deepVisibilityHeadPointerUAVDescriptorIndex)];
    RWStructuredBuffer<CLodDeepVisibilityNode> deepVisibilityNodes =
        ResourceDescriptorHeap[CLOD_RASTER_DEEP_VISIBILITY_NODE_BUFFER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> deepVisibilityNodeCounter =
        ResourceDescriptorHeap[CLOD_RASTER_DEEP_VISIBILITY_NODE_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> deepVisibilityOverflowCounter =
        ResourceDescriptorHeap[CLOD_RASTER_DEEP_VISIBILITY_OVERFLOW_COUNTER_DESCRIPTOR_INDEX];

    uint2 dims;
    deepVisibilityHeadPointers.GetDimensions(dims.x, dims.y);
    if (pixel.x >= dims.x || pixel.y >= dims.y)
    {
        return;
    }

#if defined(PSO_ALPHA_TEST)
    TestAlpha(input.texcoord, input.materialDataIndex);
#endif

    uint64_t opaqueVisKey = opaqueVisibility[pixel];
    if (opaqueVisKey != 0xFFFFFFFFFFFFFFFF)
    {
        float opaqueDepth;
        uint opaqueClusterIndex;
        uint opaquePrimID;
        UnpackVisKey(opaqueVisKey, opaqueDepth, opaqueClusterIndex, opaquePrimID);
        if (input.linearDepth >= opaqueDepth)
        {
            return;
        }
    }

    uint nodeIndex;
    InterlockedAdd(deepVisibilityNodeCounter[0], 1u, nodeIndex);
    if (nodeIndex >= CLOD_RASTER_DEEP_VISIBILITY_NODE_CAPACITY)
    {
        uint ignored;
        InterlockedAdd(deepVisibilityOverflowCounter[0], 1u, ignored);
        return;
    }

    uint previousHead;
    InterlockedExchange(deepVisibilityHeadPointers[pixel], nodeIndex, previousHead);

    CLodDeepVisibilityNode node;
    node.visKey = PackVisKey(input.linearDepth, input.visibleClusterIndex, primID);
    node.next = previousHead;
    node.flags = isFrontFace ? 0u : 1u;
    deepVisibilityNodes[nodeIndex] = node;
}
