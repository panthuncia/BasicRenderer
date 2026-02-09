#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"
#include "PerPassRootConstants/clodRootConstants.h"
#include "include/visibilityPacking.hlsli"

struct VisibilityOutput {
    uint4 debug;
};

[shader("pixel")]
VisibilityOutput VisibilityBufferPSMain(VisBufferPSInput input, bool isFrontFace : SV_IsFrontFace, uint primID : SV_PrimitiveID) : SV_TARGET
{
    // Need to check alpha for alpha-tested materials
#if defined(PSO_ALPHA_TEST)
    TestAlpha(input.texcoord, input.materialDataIndex);
#endif
    
    uint2 pixel = input.position.xy;

    // High 32 bits = depth
    uint64_t output = PackVisKey(input.linearDepth, input.visibleClusterIndex, primID);

    // Fetch view-specific output buffer
    StructuredBuffer<uint> viewVisbufferUAVIndexBuffer = ResourceDescriptorHeap[CLOD_VIEW_UAV_INDICES_BUFFER_DESCRIPTOR_INDEX];
    uint visBufferUAVIndex = viewVisbufferUAVIndexBuffer[input.viewID];
    RWTexture2D<uint64_t> visBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(visBufferUAVIndex)];

    uint2 dims;
    visBuffer.GetDimensions(dims.x, dims.y);
    if (pixel.x >= dims.x || pixel.y >= dims.y)
    {
        // Pixel outside of bounds
        return (VisibilityOutput)0;
    }

    InterlockedMin(visBuffer[pixel], output);

    VisibilityOutput result;
    result.debug = uint4(input.visibleClusterIndex, primID, 0, 0);
    return result;
}