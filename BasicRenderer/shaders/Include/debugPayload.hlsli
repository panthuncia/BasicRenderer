#ifndef __DEBUG_PAYLOAD_HLSLI__
#define __DEBUG_PAYLOAD_HLSLI__

#include "include/outputTypes.hlsli"
#include "include/cbuffers.hlsli"

// Debug visualization payload helpers.
// Any pass can write a 64-bit payload (uint2) per pixel to the debug texture.
// The resolve pass decodes based on outputType.

#define DEBUG_SENTINEL 0xFFFFFFFF

// Packing helpers

// Pack a float3 into uint2 as three float16 values (48 bits used, 16 spare).
uint2 PackDebugFloat3(float3 v)
{
    uint lo = f32tof16(v.x) | (f32tof16(v.y) << 16);
    uint hi = f32tof16(v.z);
    return uint2(lo, hi);
}

// Pack a single float into uint2.
uint2 PackDebugFloat1(float v)
{
    return uint2(asuint(v), 0);
}

// Pack a single uint into uint2 (for hashing / color-coding).
uint2 PackDebugUint(uint v)
{
    return uint2(v, 0);
}

// Pack two uints into uint2.
uint2 PackDebugUint2(uint a, uint b)
{
    return uint2(a, b);
}

// Unpacking helpers

float3 UnpackDebugFloat3(uint2 payload)
{
    float x = f16tof32(payload.x & 0xFFFF);
    float y = f16tof32(payload.x >> 16);
    float z = f16tof32(payload.y & 0xFFFF);
    return float3(x, y, z);
}

float UnpackDebugFloat1(uint2 payload)
{
    return asfloat(payload.x);
}

uint UnpackDebugUint(uint2 payload)
{
    return payload.x;
}

// Write helper

void WriteDebugPixel(RWTexture2D<uint2> debugTex, uint2 pixel, uint2 payload)
{
    debugTex[pixel] = payload;
}

// Hash-to-color for uint visualization

float3 HashToColor(uint v)
{
    // Simple hash-based deterministic color
    uint h = v;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = (h >> 16) ^ h;
    return float3(
        float((h >> 0) & 0xFF) / 255.0,
        float((h >> 8) & 0xFF) / 255.0,
        float((h >> 16) & 0xFF) / 255.0
    );
}

#endif // __DEBUG_PAYLOAD_HLSLI__
