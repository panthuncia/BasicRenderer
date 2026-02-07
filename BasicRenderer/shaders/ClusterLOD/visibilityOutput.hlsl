#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"
#include "PerPassRootConstants/clodRootConstants.h"

static const uint META_BITS   = 33;
static const uint DEPTH_BITS  = 31;
static const uint CLUSTER_BITS= 25;
static const uint TRI_BITS    = 7;

uint64_t PackVisKey(float depth01, uint clusterId, uint triId, bool isBackface)
{
    depth01 = saturate(depth01);

    uint depthQ = (uint)round(depth01 * 2147483647.0); // (2^31 - 1)

    uint faceBit = isBackface ? 1u : 0u; // front=0 preferred on ties

    uint64_t key =
        ( (uint64_t)depthQ    << META_BITS ) |
        ( (uint64_t)faceBit   << (CLUSTER_BITS + TRI_BITS) ) |
        ( (uint64_t)clusterId << TRI_BITS ) |
        ( (uint64_t)triId );

    return key;
}

void UnpackVisKey(uint64_t key, out float depth01, out uint clusterId, out uint triId, out bool isBackface)
{
    uint triMask     = (1u << TRI_BITS) - 1u;          // 0x7F
    uint clusterMask = (1u << CLUSTER_BITS) - 1u;      // 0x1FFFFFF

    triId     = (uint)( key & triMask );
    clusterId = (uint)((key >> TRI_BITS) & clusterMask);
    isBackface= (((key >> (TRI_BITS + CLUSTER_BITS)) & 1ull) != 0);

    uint depthQ = (uint)(key >> META_BITS);
    depth01 = (float)depthQ * (1.0 / 2147483647.0);
}

[shader("pixel")]
void VisibilityBufferPSMain(VisBufferPSInput input, bool isFrontFace : SV_IsFrontFace, uint primID : SV_PrimitiveID) : SV_TARGET
{
    // Need to check alpha for alpha-tested materials
#if defined(PSO_ALPHA_TEST)
    TestAlpha(input.texcoord, input.materialDataIndex);
#endif
    
    // High 32 bits = depth
    uint64_t output = PackVisKey(input.linearDepth, input.visibleClusterIndex, primID, !isFrontFace);

    // Fetch view-specific output buffer
    StructuredBuffer<uint> viewVisbufferUAVIndexBuffer = ResourceDescriptorHeap[CLOD_VIEW_UAV_INDICES_BUFFER_DESCRIPTOR_INDEX];
    uint visBufferUAVIndex = viewVisbufferUAVIndexBuffer[input.viewID];
    RWTexture2D<uint64_t> visBuffer = ResourceDescriptorHeap[NonUniformResourceIndex(visBufferUAVIndex)];
    InterlockedMin(visBuffer[uint2(input.position.xy)], output);
}