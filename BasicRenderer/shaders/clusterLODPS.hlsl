#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"

static const uint META_BITS = 33;
static const uint DEPTH_BITS = 31;
static const uint CLUSTER_BITS = 25;
static const uint TRI_BITS = 7;

uint64_t PackVisKey(float depth01, uint clusterId, uint triId, bool isBackface)
{
    depth01 = saturate(depth01);

    uint depthQ = (uint) round(depth01 * 2147483647.0); // (2^31 - 1)

    uint faceBit = isBackface ? 1u : 0u;

    uint64_t key =
        ((uint64_t) depthQ << META_BITS) |
        ((uint64_t) faceBit << (CLUSTER_BITS + TRI_BITS)) |
        ((uint64_t) clusterId << TRI_BITS) |
        ((uint64_t) triId);

    return key;
}

void UnpackVisKey(uint64_t key, out float depth01, out uint clusterId, out uint triId, out bool isBackface)
{
    uint triMask = (1u << TRI_BITS) - 1u; // 0x7F
    uint clusterMask = (1u << CLUSTER_BITS) - 1u; // 0x1FFFFFF

    triId = (uint) (key & triMask);
    clusterId = (uint) ((key >> TRI_BITS) & clusterMask);
    isBackface = (((key >> (TRI_BITS + CLUSTER_BITS)) & 1ull) != 0);

    uint depthQ = (uint) (key >> META_BITS);
    depth01 = (float) depthQ * (1.0 / 2147483647.0);
}

void ClusterLODPSMain(VisBufferPSInput input, bool isFrontFace : SV_IsFrontFace, uint primID : SV_PrimitiveID) : SV_TARGET
{
    // Need to check alpha for alpha-tested materials
#if defined(PSO_ALPHA_TEST)
    TestAlpha(input.texcoord);
#endif
    
    float depth01 = 1.0 - input.linearDepth;

    uint64_t visKey = PackVisKey(depth01, input.visibleClusterIndex, primID, !isFrontFace);

}