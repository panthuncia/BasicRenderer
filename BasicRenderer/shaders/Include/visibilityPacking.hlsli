#ifndef VISIBILITY_PACKING_HLSL
#define VISIBILITY_PACKING_HLSL

// 32 (depth) | 25 (cluster) | 7 (tri)
static const uint TRI_BITS = 7;
static const uint CLUSTER_BITS = 25;
static const uint META_BITS = TRI_BITS + CLUSTER_BITS; // 32

uint64_t PackVisKey(float depth, uint clusterId, uint triId)
{
    // Raw IEEE-754 bits (order-preserving for [0,1] non-NaN floats)
    uint depthBits = asuint(depth);

    uint64_t key =
        ((uint64_t) depthBits << META_BITS) |
        ((uint64_t) clusterId << TRI_BITS) |
        ((uint64_t) triId);

    return key;
}

void UnpackVisKey(uint64_t key, out float depth, out uint clusterId, out uint triId)
{
    const uint triMask = (1u << TRI_BITS) - 1u; // 0x7F
    const uint clusterMask = (1u << CLUSTER_BITS) - 1u; // 0x1FFFFFF

    triId = (uint) (key & triMask);
    clusterId = (uint) ((key >> TRI_BITS) & clusterMask);

    uint depthBits = (uint) (key >> META_BITS);
    depth = asfloat(depthBits);
}

#endif // VISIBILITY_PACKING_HLSL