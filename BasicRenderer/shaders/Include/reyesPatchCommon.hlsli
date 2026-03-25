#ifndef __REYES_PATCH_COMMON_HLSLI__
#define __REYES_PATCH_COMMON_HLSLI__

#include "include/dynamicSwizzle.hlsli"

static const float REYES_PATCH_BARYCENTRIC_COORD_SCALE = 65535.0f;
static const float REYES_BARYCENTRIC_COORD_SCALE = REYES_PATCH_BARYCENTRIC_COORD_SCALE;

float3 ReyesDecodePatchBarycentrics(uint encoded)
{
    float u = (float)(encoded & 0xFFFFu) / REYES_PATCH_BARYCENTRIC_COORD_SCALE;
    float v = (float)(encoded >> 16u) / REYES_PATCH_BARYCENTRIC_COORD_SCALE;
    return float3(saturate(1.0f - u - v), u, v);
}

uint ReyesGetDicePatchSegments(CLodReyesDiceQueueEntry diceEntry)
{
    return max(1u, (diceEntry.quantizedTessFactor + 255u) >> 8u);
}

float3 ReyesMakePatchGridBarycentrics(uint col, uint row, uint tessSegments)
{
    const float invSegments = 1.0f / float(max(tessSegments, 1u));
    const float u = float(col) * invSegments;
    const float v = float(row) * invSegments;
    return float3(saturate(1.0f - u - v), u, v);
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
{
    uint remaining = triIndex;
    uint row = 0u;
    [loop]
    for (; row < tessSegments; ++row)
    {
        const uint rowTriangleCount = 2u * (tessSegments - row) - 1u;
        if (remaining < rowTriangleCount)
        {
            break;
        }

        remaining -= rowTriangleCount;
    }

    const uint col = remaining >> 1u;
    const bool isDownTriangle = (remaining & 1u) != 0u;

    if (!isDownTriangle)
    {
        bary0 = ReyesMakePatchGridBarycentrics(col, row, tessSegments);
        bary1 = ReyesMakePatchGridBarycentrics(col + 1u, row, tessSegments);
        bary2 = ReyesMakePatchGridBarycentrics(col, row + 1u, tessSegments);
        return;
    }

    bary0 = ReyesMakePatchGridBarycentrics(col + 1u, row, tessSegments);
    bary1 = ReyesMakePatchGridBarycentrics(col + 1u, row + 1u, tessSegments);
    bary2 = ReyesMakePatchGridBarycentrics(col, row + 1u, tessSegments);
}

#endif // __REYES_PATCH_COMMON_HLSLI__