#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"
#include "PerPassRootConstants/clodRootConstants.h"
#include "include/visibilityPacking.hlsli"

[shader("pixel")]
void VisibilityBufferPSMain(VisBufferPSInput input, bool isFrontFace : SV_IsFrontFace, uint primID : SV_PrimitiveID) : SV_TARGET
{
    // Need to check alpha for alpha-tested materials
#if defined(PSO_ALPHA_TEST)
    TestAlpha(input.texcoord, input.materialDataIndex);
#endif
    
    // High 32 bits = depth
    uint64_t output = PackVisKey(input.linearDepth, input.visibleClusterIndex, primID);

    // Fetch view-specific output buffer
    StructuredBuffer<uint> viewVisbufferUAVIndexBuffer = ResourceDescriptorHeap[CLOD_VIEW_UAV_INDICES_BUFFER_DESCRIPTOR_INDEX];
    uint visBufferUAVIndex = viewVisbufferUAVIndexBuffer[input.viewID];
    RWTexture2D<uint64_t> visBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(visBufferUAVIndex)];
    InterlockedMin(visBuffer[uint2(input.position.xy)], output);
}