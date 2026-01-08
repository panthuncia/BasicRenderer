#include "include/cbuffers.hlsli"

#define A_GPU
#define A_HLSL

#include "FidelityFX/ffx_a.h"

// Per-dispatch global counter buffer (one uint per slice)
groupshared AU1 spdCounter;

#ifndef SPD_PACKED_ONLY
groupshared AF1 spdIntermediateR[16][16];
groupshared AF1 spdIntermediateG[16][16];
groupshared AF1 spdIntermediateB[16][16];
groupshared AF1 spdIntermediateA[16][16];

static const uint MIPMAP_FLAG_SRGB = 1u;

// Must match CPU layout
struct MipmapSpdConstants
{
    uint2 srcSize;
    uint mips; // number of mips to generate (outputs), e.g. mipLevels-1
    uint numWorkGroups;

    uint2 workGroupOffset;
    float2 invInputSize; // not strictly required for Load-based path

    uint mipUavDescriptorIndices[12]; // mip1..mip12
    uint flags;
    uint srcMip; // usually 0
    uint pad0;
    uint pad1;
};

float3 LinearToSrgb(float3 c)
{
    c = saturate(c);

    float3 lo = c * 12.92f;
    float3 hi = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;

    float3 useHi = step(0.0031308f, c); // 0 when c < edge, 1 when c >= edge
    return lerp(lo, hi, useHi);
}

float4 EncodeForOutput(float4 v, uint flags)
{
    if ((flags & MIPMAP_FLAG_SRGB) != 0u)
    {
        v.rgb = LinearToSrgb(v.rgb);
    }
    return v;
}

AF4 SpdLoadSourceImage(ASU2 p, AU1 slice)
{
    StructuredBuffer<MipmapSpdConstants> constantsBuf = ResourceDescriptorHeap[UintRootConstant2];
    MipmapSpdConstants c = constantsBuf[UintRootConstant3];

    uint2 q = min(p, max(uint2(1, 1), c.srcSize) - 1);

#if defined(MIPMAP_SCALAR)
#if defined(MIPMAP_ARRAY)
        Texture2DArray<float> imgSrc = ResourceDescriptorHeap[UintRootConstant1];
        float v = imgSrc.Load(int4(q, slice, (int)c.srcMip));
#else
        Texture2D<float> imgSrc = ResourceDescriptorHeap[UintRootConstant1];
        float v = imgSrc.Load(int3(q, (int)c.srcMip));
#endif
    return AF4(v, v, v, v);
#else
#if defined(MIPMAP_ARRAY)
        Texture2DArray<float4> imgSrc = ResourceDescriptorHeap[UintRootConstant1];
        float4 v = imgSrc.Load(int4(q, slice, (int)c.srcMip));
#else
    Texture2D<float4> imgSrc = ResourceDescriptorHeap[UintRootConstant1];
    float4 v = imgSrc.Load(int3(q, (int) c.srcMip));
#endif
    return AF4(v.x, v.y, v.z, v.w);
#endif
}

AF4 SpdLoad(ASU2 tex, AU1 slice)
{
    StructuredBuffer<MipmapSpdConstants> constantsBuf = ResourceDescriptorHeap[UintRootConstant2];
    MipmapSpdConstants c = constantsBuf[UintRootConstant3];

#if defined(MIPMAP_SCALAR)
#if defined(MIPMAP_ARRAY)
        globallycoherent RWTexture2DArray<float> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
        return imgDst5[float3(tex, slice)].rrrr;
#else
        globallycoherent RWTexture2D<float> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
        return imgDst5[tex].rrrr;
#endif
#else
#if defined(MIPMAP_ARRAY)
        globallycoherent RWTexture2DArray<float4> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
        float4 v = imgDst5[float3(tex, slice)];
        return AF4(v.x, v.y, v.z, v.w);
#else
    globallycoherent RWTexture2D<float4> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
    float4 v = imgDst5[tex];
    return AF4(v.x, v.y, v.z, v.w);
#endif
#endif
}

void SpdStore(ASU2 pix, AF4 outValue, AU1 index, AU1 slice)
{
    StructuredBuffer<MipmapSpdConstants> constantsBuf = ResourceDescriptorHeap[UintRootConstant2];
    MipmapSpdConstants c = constantsBuf[UintRootConstant3];

#if defined(MIPMAP_SCALAR)
    float v = outValue.x;
    if (index == 5)
    {
#if defined(MIPMAP_ARRAY)
            globallycoherent RWTexture2DArray<float> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
            imgDst5[float3(pix, slice)] = v;
#else
            globallycoherent RWTexture2D<float> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
            imgDst5[pix] = v;
#endif
        return;
    }

#if defined(MIPMAP_ARRAY)
        RWTexture2DArray<float> imgDst = ResourceDescriptorHeap[NonUniformResourceIndex(c.mipUavDescriptorIndices[index])];
        imgDst[float3(pix, slice)] = v;
#else
        RWTexture2D<float> imgDst = ResourceDescriptorHeap[NonUniformResourceIndex(c.mipUavDescriptorIndices[index])];
        imgDst[pix] = v;
#endif

#else
    float4 v = EncodeForOutput(float4(outValue.x, outValue.y, outValue.z, outValue.w), c.flags);

    if (index == 5)
    {
#if defined(MIPMAP_ARRAY)
            globallycoherent RWTexture2DArray<float4> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
            imgDst5[float3(pix, slice)] = v;
#else
        globallycoherent RWTexture2D<float4> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
        imgDst5[pix] = v;
#endif
        return;
    }

#if defined(MIPMAP_ARRAY)
        RWTexture2DArray<float4> imgDst = ResourceDescriptorHeap[NonUniformResourceIndex(c.mipUavDescriptorIndices[index])];
        imgDst[float3(pix, slice)] = v;
#else
    RWTexture2D<float4> imgDst = ResourceDescriptorHeap[NonUniformResourceIndex(c.mipUavDescriptorIndices[index])];
    imgDst[pix] = v;
#endif
#endif
}

AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3)
{
    return (v0 + v1 + v2 + v3) * 0.25f;
}

void SpdIncreaseAtomicCounter(AU1 slice)
{
    globallycoherent RWStructuredBuffer<uint> spdGlobalAtomic = ResourceDescriptorHeap[UintRootConstant0];
    InterlockedAdd(spdGlobalAtomic[slice], 1, spdCounter);
}
AU1 SpdGetAtomicCounter()
{
    return spdCounter;
}
void SpdResetAtomicCounter(AU1 slice)
{
    globallycoherent RWStructuredBuffer<uint> spdGlobalAtomic = ResourceDescriptorHeap[UintRootConstant0];
    spdGlobalAtomic[slice] = 0;
}

AF4 SpdLoadIntermediate(AU1 x, AU1 y)
{
    return AF4(
        spdIntermediateR[x][y],
        spdIntermediateG[x][y],
        spdIntermediateB[x][y],
        spdIntermediateA[x][y]);
}

void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value)
{
    spdIntermediateR[x][y] = value.x;
    spdIntermediateG[x][y] = value.y;
    spdIntermediateB[x][y] = value.z;
    spdIntermediateA[x][y] = value.w;
}
#endif // SPD_PACKED_ONLY

#include "FidelityFX/ffx_spd.h"

[numthreads(256, 1, 1)]
void MipmapCSMain(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
    StructuredBuffer<MipmapSpdConstants> constantsBuf = ResourceDescriptorHeap[UintRootConstant2];
    MipmapSpdConstants c = constantsBuf[UintRootConstant3];

    SpdDownsample(
        AU2(WorkGroupId.xy),
        AU1(LocalThreadIndex),
        AU1(c.mips),
        AU1(c.numWorkGroups),
        AU1(WorkGroupId.z),
        AU2(c.workGroupOffset));
}
