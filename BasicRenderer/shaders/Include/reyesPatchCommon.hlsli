#ifndef __REYES_PATCH_COMMON_HLSLI__
#define __REYES_PATCH_COMMON_HLSLI__

#include "include/dynamicSwizzle.hlsli"

static const float REYES_PATCH_BARYCENTRIC_COORD_SCALE = 65535.0f;
static const float REYES_BARYCENTRIC_COORD_SCALE = REYES_PATCH_BARYCENTRIC_COORD_SCALE;
static const uint REYES_MAX_VISIBILITY_MICRO_TRIANGLES_PER_PATCH = 128u;
static const uint REYES_TESS_TABLE_LOOKUP_INDEX_MASK = 0x7FFFu;
static const uint REYES_TESS_TABLE_FLIP_BIT = 1u << 15u;

float3 ReyesDecodePatchBarycentrics(uint encoded)
{
    float u = (float)(encoded & 0xFFFFu) / REYES_PATCH_BARYCENTRIC_COORD_SCALE;
    float v = (float)(encoded >> 16u) / REYES_PATCH_BARYCENTRIC_COORD_SCALE;
    return float3(saturate(1.0f - u - v), u, v);
}

CLodReyesTessTableConfigEntry ReyesGetDicePatchConfig(
    StructuredBuffer<CLodReyesTessTableConfigEntry> tessTableConfigs,
    CLodReyesDiceQueueEntry diceEntry)
{
    return tessTableConfigs[diceEntry.tessTableConfigIndex & REYES_TESS_TABLE_LOOKUP_INDEX_MASK];
}

uint ReyesGetDicePatchMicroTriangleCount(
    StructuredBuffer<CLodReyesTessTableConfigEntry> tessTableConfigs,
    CLodReyesDiceQueueEntry diceEntry)
{
    return ReyesGetDicePatchConfig(tessTableConfigs, diceEntry).numTriangles;
}

uint ReyesGetDicePatchVertexCount(
    StructuredBuffer<CLodReyesTessTableConfigEntry> tessTableConfigs,
    CLodReyesDiceQueueEntry diceEntry)
{
    return ReyesGetDicePatchConfig(tessTableConfigs, diceEntry).numVertices;
}

float3 ReyesDecodeDicePatchVertexBarycentrics(
    StructuredBuffer<CLodReyesTessTableConfigEntry> tessTableConfigs,
    StructuredBuffer<uint> tessTableVertices,
    CLodReyesDiceQueueEntry diceEntry,
    uint vertexIndex)
{
    const CLodReyesTessTableConfigEntry configEntry = ReyesGetDicePatchConfig(tessTableConfigs, diceEntry);
    float3 barycentrics = ReyesDecodePatchBarycentrics(tessTableVertices[configEntry.firstVertex + vertexIndex]);
    if ((diceEntry.tessTableConfigIndex & REYES_TESS_TABLE_FLIP_BIT) != 0u)
    {
        barycentrics = barycentrics.yxz;
    }

    return barycentrics;
}

uint3 ReyesDecodeDicePatchTriangleIndices(
    StructuredBuffer<CLodReyesTessTableConfigEntry> tessTableConfigs,
    StructuredBuffer<uint> tessTableTriangles,
    CLodReyesDiceQueueEntry diceEntry,
    uint triangleIndex)
{
    const CLodReyesTessTableConfigEntry configEntry = ReyesGetDicePatchConfig(tessTableConfigs, diceEntry);
    const uint packedTriangle = tessTableTriangles[configEntry.firstTriangle + triangleIndex];
    uint3 indices = uint3(
        packedTriangle & 0xFFu,
        (packedTriangle >> 8u) & 0xFFu,
        (packedTriangle >> 16u) & 0xFFu);
    if ((diceEntry.tessTableConfigIndex & REYES_TESS_TABLE_FLIP_BIT) != 0u)
    {
        indices = indices.xzy;
    }

    return indices;
}

float3 ReyesComposeSourceBarycentricsPoint(float3 patchBary, float3 domain0, float3 domain1, float3 domain2)
{
    return
        domain0 * patchBary.x +
        domain1 * patchBary.y +
        domain2 * patchBary.z;
}

float ReyesSampleDisplacementOffset(MaterialInfo materialInfo, float2 uv)
{
    if (materialInfo.geometricDisplacementEnabled == 0u)
    {
        return 0.0f;
    }

    Texture2D<float4> heightTexture = ResourceDescriptorHeap[NonUniformResourceIndex(materialInfo.heightMapIndex)];
    SamplerState heightSampler = SamplerDescriptorHeap[NonUniformResourceIndex(materialInfo.heightSamplerIndex)];
    const float4 heightSample = heightTexture.SampleLevel(heightSampler, uv, 0.0f);
    const float heightValue = saturate(DynamicSwizzle(heightSample, materialInfo.heightChannel));
    return lerp(materialInfo.geometricDisplacementMin, materialInfo.geometricDisplacementMax, heightValue);
}

float3 ReyesApplyGeometricDisplacement(MaterialInfo materialInfo, float3 positionOS, float3 normalOS, float2 uv)
{
    const float displacementOffset = ReyesSampleDisplacementOffset(materialInfo, uv);
    return positionOS + normalize(normalOS) * displacementOffset;
}

void ReyesDecodeMicroTrianglePatchDomain(uint triIndex, uint tessSegments, out float3 bary0, out float3 bary1, out float3 bary2)
;

void ReyesDecodeMicroTrianglePatchDomain(
    StructuredBuffer<CLodReyesTessTableConfigEntry> tessTableConfigs,
    StructuredBuffer<uint> tessTableVertices,
    StructuredBuffer<uint> tessTableTriangles,
    CLodReyesDiceQueueEntry diceEntry,
    uint triIndex,
    out float3 bary0,
    out float3 bary1,
    out float3 bary2)
{
    const uint3 localTriangleIndices = ReyesDecodeDicePatchTriangleIndices(tessTableConfigs, tessTableTriangles, diceEntry, triIndex);
    bary0 = ReyesDecodeDicePatchVertexBarycentrics(tessTableConfigs, tessTableVertices, diceEntry, localTriangleIndices.x);
    bary1 = ReyesDecodeDicePatchVertexBarycentrics(tessTableConfigs, tessTableVertices, diceEntry, localTriangleIndices.y);
    bary2 = ReyesDecodeDicePatchVertexBarycentrics(tessTableConfigs, tessTableVertices, diceEntry, localTriangleIndices.z);
}

#endif // __REYES_PATCH_COMMON_HLSLI__