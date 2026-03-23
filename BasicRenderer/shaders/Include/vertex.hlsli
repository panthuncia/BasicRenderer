#ifndef __VERTEX_HLSL__
#define __VERTEX_HLSL__

#include "include/loadingUtils.hlsli"

// Manually assembled from ByteAddressBuffer
struct SkinningInfluences
{
    uint4 joints0;
    uint4 joints1;
    float4 weights0;
    float4 weights1;
};

struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    SkinningInfluences skinning;
    float3 color;
};

#define VERTEX_COLORS 1 << 0
#define VERTEX_NORMAL 1 << 1
#define VERTEX_TEXCOORDS 1 << 2
#define VERTEX_SKINNED 1 << 3

// Per-object flags (mirrors OBJECT_FLAG_* in ShaderBuffers.h)
#define OBJECT_FLAG_REVERSE_WINDING (1u << 0)

Vertex LoadVertex(uint byteOffset, ByteAddressBuffer buffer, uint flags) {
    Vertex vertex;

    // Load position (float3, 12 bytes)
    vertex.position = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;

    // Load normal (float3, 12 bytes)
    vertex.normal = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;

    vertex.skinning.joints0 = uint4(0, 0, 0, 0);
    vertex.skinning.joints1 = uint4(0, 0, 0, 0);
    vertex.skinning.weights0 = float4(0.0, 0.0, 0.0, 0.0);
    vertex.skinning.weights1 = float4(0.0, 0.0, 0.0, 0.0);

    if (flags & VERTEX_TEXCOORDS) {
        // Load texcoord (float2, 8 bytes)
        vertex.texcoord = LoadFloat2(byteOffset, buffer);
        byteOffset += 8;
    }
    else
    {
        vertex.texcoord = float2(0.0, 0.0);
    }

    vertex.color = float3(1.0, 1.0, 1.0);

    return vertex;
}

Vertex LoadSkinningVertex(uint byteOffset, ByteAddressBuffer buffer, uint flags)
{
    Vertex vertex;

    // Load position (float3, 12 bytes)
    vertex.position = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;

    // Load normal (float3, 12 bytes)
    vertex.normal = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;

    vertex.texcoord = float2(0.0, 0.0);
    vertex.skinning.joints0 = uint4(0, 0, 0, 0);
    vertex.skinning.joints1 = uint4(0, 0, 0, 0);
    vertex.skinning.weights0 = float4(0.0, 0.0, 0.0, 0.0);
    vertex.skinning.weights1 = float4(0.0, 0.0, 0.0, 0.0);

    if (flags & VERTEX_SKINNED) {
        // Load joints (uint4x2, 32 bytes)
        vertex.skinning.joints0 = LoadUint4(byteOffset, buffer);
        byteOffset += 16;
        vertex.skinning.joints1 = LoadUint4(byteOffset, buffer);
        byteOffset += 16;

        // Load weights (float4x2, 32 bytes)
        vertex.skinning.weights0 = LoadFloat4(byteOffset, buffer);
        byteOffset += 16;
        vertex.skinning.weights1 = LoadFloat4(byteOffset, buffer);
        byteOffset += 16;
    }
    
    vertex.color = float3(1.0, 1.0, 1.0);

    return vertex;
}

#endif // __VERTEX_HLSL__
