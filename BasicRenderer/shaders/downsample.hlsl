#include "cbuffers.hlsli"

#define A_GPU
#define A_HLSL
#define SPD_LINEAR_SAMPLER

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

struct spdConstants
{
    uint mips;
    uint numWorkGroups;
    uint2 workGroupOffset;
    float2 invInputSize;
    unsigned int mipUavDescriptorIndices[11];
    uint pad[3];
};

// UintRootConstant0 is the index of the global atomic buffer
// UintRootConstant1 is the index of the source image
// UintRootConstant2 is the index of the spdConstants structured buffer
// UintRootConstant3 is the index of the constants in the spdConstants structured buffer

AF4 SpdLoadSourceImage(ASU2 p, AU1 slice)
{
    StructuredBuffer<spdConstants> constants = ResourceDescriptorHeap[UintRootConstant2];
    float2 invInputSize = constants[UintRootConstant3].invInputSize;
    AF2 textureCoord = p * invInputSize + invInputSize;
    
    #if defined (DOWNSAMPLE_ARRAY)
    Texture2DArray<float> imgSrc = ResourceDescriptorHeap[UintRootConstant1];
    float result = imgSrc.SampleLevel(g_linearClamp, float3(textureCoord, slice), 0);
    #else
    Texture2D<float> imgSrc = ResourceDescriptorHeap[UintRootConstant1];
    float result = imgSrc.SampleLevel(g_linearClamp, textureCoord, 0);
    #endif
    return AF4(result, result, result, result);
}
AF4 SpdLoad(ASU2 tex, AU1 slice)
{
    StructuredBuffer<spdConstants> constants = ResourceDescriptorHeap[UintRootConstant2];
    #if defined (DOWNSAMPLE_ARRAY)
    globallycoherent RWTexture2DArray<float4> imgDst5 = ResourceDescriptorHeap[constants[UintRootConstant3].mipUavDescriptorIndices[5]];
    return imgDst5[float3(tex, slice)];
    #else
    globallycoherent RWTexture2D<float4> imgDst5 = ResourceDescriptorHeap[constants[UintRootConstant3].mipUavDescriptorIndices[5]];
    return imgDst5[tex];
    #endif
}
void SpdStore(ASU2 pix, AF4 outValue, AU1 index, AU1 slice)
{
    StructuredBuffer<spdConstants> constants = ResourceDescriptorHeap[UintRootConstant2];
    if (index == 5)
    {
        #if defined (DOWNSAMPLE_ARRAY)
        globallycoherent RWTexture2DArray<float4> imgDst5 = ResourceDescriptorHeap[constants[UintRootConstant3].mipUavDescriptorIndices[5]];
        imgDst5[float3(pix, slice)] = outValue;
        #else
        globallycoherent RWTexture2D<float4> imgDst5 = ResourceDescriptorHeap[constants[UintRootConstant3].mipUavDescriptorIndices[5]];
        imgDst5[pix] = outValue;
        #endif
        return;
    }
    #if defined (DOWNSAMPLE_ARRAY)
    RWTexture2DArray<float4> imgDst = ResourceDescriptorHeap[constants[UintRootConstant3].mipUavDescriptorIndices[index]];
    imgDst[float3(pix, slice)] = outValue;
    #else
    RWTexture2D<float4> imgDst = ResourceDescriptorHeap[constants[UintRootConstant3].mipUavDescriptorIndices[index]];
    imgDst[pix] = outValue;
    #endif
}

AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3)
{
    float m = max(max(v0.x, v1.x), max(v2.x, v3.x));
    return AF4(m, m, m, m);
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
#endif

#include "FidelityFX/ffx_spd.h"

// Main function
//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
[numthreads(256, 1, 1)]
void DownsampleCSMain(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
    StructuredBuffer<spdConstants> constantsBuf = ResourceDescriptorHeap[UintRootConstant2];
    spdConstants constants = constantsBuf[UintRootConstant3];
    SpdDownsample(
        AU2(WorkGroupId.xy),
        AU1(LocalThreadIndex),
        AU1(constants.mips),
        AU1(constants.numWorkGroups),
        AU1(WorkGroupId.z),
        AU2(constants.workGroupOffset));
}