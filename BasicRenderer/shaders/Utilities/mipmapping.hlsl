#include "include/cbuffers.hlsli"

#define A_GPU
#define A_HLSL

// We use Load(), so no sampler is required.
// (Keeping SPD_LINEAR_SAMPLER undefined is fine.)

// Same style as your downsample pass.
// NOTE: This limits per-dispatch slices to 6 (slice is used as counter index).
struct SpdGlobalAtomicBuffer
{
    uint counter[6];
};

#include "FidelityFX/ffx_a.h"

groupshared AU1 spdCounter;

#ifndef SPD_PACKED_ONLY
groupshared AF1 spdIntermediateR[16][16];
groupshared AF1 spdIntermediateG[16][16];
groupshared AF1 spdIntermediateB[16][16];
groupshared AF1 spdIntermediateA[16][16];

// UintRootConstant0 = atomic counter buffer descriptor index
// UintRootConstant1 = source SRV descriptor index (mip source for this dispatch)
// UintRootConstant2 = constants structured buffer SRV descriptor index
// UintRootConstant3 = constants element index in structured buffer

// Max "extra mips" generated per dispatch = 11 (mip1..mip11 relative to the source mip).
#define SPD_MAX_GENERATED_MIPS 11

// flags: bit0 = sRGB encode on store (RGB only; alpha stays linear)
struct MipmapSpdConstants
{
    uint2 srcSize; // size of the *source mip*
    uint mips; // how many mips to generate this dispatch (<= 11)
    uint numWorkGroups;

    uint2 workGroupOffset;
    float2 invInputSize;

    uint srcMip;
    uint flags; // bit0: sRGB encode on store (RGB only)

    uint mipUavDescriptorIndices[11]; // destinations: srcMip+1 ... srcMip+mips
    uint pad[3];
};


static float LinearToSrgb1(float c)
{
    c = saturate(c);
    if (c <= 0.0031308f)
        return 12.92f * c;
    return 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
}

static float3 LinearToSrgb3(float3 c)
{
    return float3(LinearToSrgb1(c.x), LinearToSrgb1(c.y), LinearToSrgb1(c.z));
}

// SPD hook: source load
// We clamp p to srcSize-1 to handle odd sizes.
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
		float v = imgSrc.Load(int3(q, (int) c.srcMip));
#endif
    return AF4(v, v, v, v);
#else
#if defined(MIPMAP_ARRAY)
    Texture2DArray<float4> imgSrc = ResourceDescriptorHeap[UintRootConstant1];
    float4 v = imgSrc.Load(int4(q, slice, (int)c.srcMip)); // mip-select
#else
    Texture2D<float4> imgSrc = ResourceDescriptorHeap[UintRootConstant1];
    float4 v = imgSrc.Load(int3(q, (int) c.srcMip)); // mip-select
#endif
    return AF4(v.x, v.y, v.z, v.w);
#endif
}

// SPD hook: load from mip 5 (relative) for cross-workgroup.
AF4 SpdLoad(ASU2 tex, AU1 slice)
{
    StructuredBuffer<MipmapSpdConstants> constantsBuf = ResourceDescriptorHeap[UintRootConstant2];
    MipmapSpdConstants c = constantsBuf[UintRootConstant3];

#if defined(MIPMAP_SCALAR)
#if defined(MIPMAP_ARRAY)
        globallycoherent RWTexture2DArray<float> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
        float v = imgDst5[float3(tex, slice)];
#else
        globallycoherent RWTexture2D<float> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
        float v = imgDst5[tex];
#endif
    return AF4(v, v, v, v);
#else
#if defined(MIPMAP_ARRAY)
        globallycoherent RWTexture2DArray<float4> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
        return AF4(imgDst5[float3(tex, slice)]);
#else
    globallycoherent RWTexture2D<float4> imgDst5 = ResourceDescriptorHeap[c.mipUavDescriptorIndices[5]];
    return AF4(imgDst5[tex]);
#endif
#endif
}

#ifndef MIPMAP_SCALAR
static float4 ApplyStoreEncoding(MipmapSpdConstants c, float4 v)
{
    if ((c.flags & 1u) != 0u)
    {
        v.rgb = LinearToSrgb3(v.rgb);
    }
    return v;
}
#endif

// SPD hook: store to mip index (0 => mip1 relative).
// index==5 uses globallycoherent for the SPD cross-workgroup path.
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
    float4 v = float4(outValue.x, outValue.y, outValue.z, outValue.w);
    v = ApplyStoreEncoding(c, v);

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
    globallycoherent RWStructuredBuffer<SpdGlobalAtomicBuffer> spdGlobalAtomic = ResourceDescriptorHeap[UintRootConstant0];
    InterlockedAdd(spdGlobalAtomic[0].counter[slice], 1, spdCounter);
}
AU1 SpdGetAtomicCounter()
{
    return spdCounter;
}
void SpdResetAtomicCounter(AU1 slice)
{
    globallycoherent RWStructuredBuffer<SpdGlobalAtomicBuffer> spdGlobalAtomic = ResourceDescriptorHeap[UintRootConstant0];
    spdGlobalAtomic[0].counter[slice] = 0;
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
