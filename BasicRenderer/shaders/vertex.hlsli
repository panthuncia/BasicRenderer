#ifndef __VERTEX_HLSL__
#define __VERTEX_HLSL__

#include "utilities.hlsli"
// Manually assembled from ByteAddressBuffer
struct Vertex {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    uint4 joints : TEXCOORD1;
    float4 weights : TEXCOORD2;
    float3 color;
};

struct PSInput {
    float4 position : SV_POSITION; // Screen-space position, required for rasterization
    float4 positionWorldSpace : TEXCOORD0; // For world-space lighting
    float4 positionViewSpace : TEXCOORD1; // For cascaded shadows
    float3 normalWorldSpace : TEXCOORD2; // For world-space lighting
    float2 texcoord : TEXCOORD3;
    float3 color : TEXCOORD7; // For models with vertex colors
    float3 normalModelSpace : TEXCOORD8; // For debug view
    uint meshletIndex : TEXCOORD9; // For meshlet debug view
};

#define VERTEX_COLORS 1 << 0
#define VERTEX_NORMAL 1 << 1
#define VERTEX_TEXCOORDS 1 << 2
#define VERTEX_SKINNED 1 << 3

Vertex LoadVertex(uint byteOffset, ByteAddressBuffer buffer, uint flags) {
    Vertex vertex;

    // Load position (float3, 12 bytes)
    vertex.position = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;

    // Load normal (float3, 12 bytes)
    vertex.normal = LoadFloat3(byteOffset, buffer);
    byteOffset += 12;

    if (flags & VERTEX_TEXCOORDS) {
        // Load texcoord (float2, 8 bytes)
        vertex.texcoord = LoadFloat2(byteOffset, buffer);
        byteOffset += 8;
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

    if (flags & VERTEX_SKINNED) {
        // Load joints (uint4, 16 bytes)
        vertex.joints = LoadUint4(byteOffset, buffer);
        byteOffset += 16;

        // Load weights (float4, 16 bytes)
        vertex.weights = LoadFloat4(byteOffset, buffer);
        byteOffset += 16;
    }
    
    return vertex;
}

#endif // __VERTEX_HLSL__