#include "../include/cbuffers.hlsli"

static const uint BC7Mode6Weights[16] = {
    0u, 4u, 9u, 13u,
    17u, 21u, 26u, 30u,
    34u, 38u, 43u, 47u,
    51u, 55u, 60u, 64u
};

struct BC7Mode6Block
{
    uint4 words;
    uint bitPosition;
};

uint FloatToByte(float value)
{
    return (uint)round(saturate(value) * 255.0f);
}

void WriteBits(inout BC7Mode6Block block, uint bitCount, uint value)
{
    for (uint bit = 0; bit < bitCount; ++bit)
    {
        const uint absoluteBit = block.bitPosition + bit;
        const uint wordIndex = absoluteBit >> 5u;
        const uint bitIndex = absoluteBit & 31u;
        if (((value >> bit) & 1u) != 0u)
        {
            block.words[wordIndex] |= (1u << bitIndex);
        }
    }

    block.bitPosition += bitCount;
}

uint ReconstructEndpoint(uint q7, uint pbit)
{
    return min(255u, (q7 << 1u) | pbit);
}

uint2 QuantizeEndpoint(float4 endpoint)
{
    uint endpointBytes[4] = {
        FloatToByte(endpoint.x),
        FloatToByte(endpoint.y),
        FloatToByte(endpoint.z),
        FloatToByte(endpoint.w)
    };

    uint bestPbit = 0u;
    uint bestError = 0xffffffffu;
    uint bestQ7Packed = 0u;

    [unroll]
    for (uint pbit = 0u; pbit < 2u; ++pbit)
    {
        uint totalError = 0u;
        uint packedQ7 = 0u;

        [unroll]
        for (uint component = 0u; component < 4u; ++component)
        {
            const uint value = endpointBytes[component];
            const uint q7 = min(127u, (value + 1u - pbit) >> 1u);
            const uint reconstructed = ReconstructEndpoint(q7, pbit);
            const int delta = int(value) - int(reconstructed);
            totalError += uint(delta * delta);
            packedQ7 |= (q7 & 0x7fu) << (component * 8u);
        }

        if (totalError < bestError)
        {
            bestError = totalError;
            bestPbit = pbit;
            bestQ7Packed = packedQ7;
        }
    }

    return uint2(bestQ7Packed, bestPbit);
}

uint4 LoadBlockPixels(Texture2D<float4> srcTexture, uint2 blockBase, uint2 textureSize, out uint4 alphaHigh)
{
    uint4 minRGBA = uint4(255u, 255u, 255u, 255u);
    uint4 maxRGBA = uint4(0u, 0u, 0u, 0u);
    alphaHigh = uint4(0u, 0u, 0u, 0u);

    [unroll]
    for (uint y = 0u; y < 4u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 4u; ++x)
        {
            const uint2 pixel = min(blockBase + uint2(x, y), textureSize - 1u.xx);
            const float4 sampleValue = srcTexture.Load(int3(pixel, 0));
            const uint4 rgba = uint4(
                FloatToByte(sampleValue.x),
                FloatToByte(sampleValue.y),
                FloatToByte(sampleValue.z),
                FloatToByte(sampleValue.w));
            minRGBA = min(minRGBA, rgba);
            maxRGBA = max(maxRGBA, rgba);
        }
    }

    return uint4(minRGBA.xyz, minRGBA.w) | (uint4(maxRGBA.xyz, maxRGBA.w) << 16u);
}

float4 DecodeMinEndpoint(uint4 packedMinMax)
{
    return float4(
        float(packedMinMax.x & 0xffffu),
        float(packedMinMax.y & 0xffffu),
        float(packedMinMax.z & 0xffffu),
        float(packedMinMax.w & 0xffffu)) * (1.0f / 255.0f);
}

float4 DecodeMaxEndpoint(uint4 packedMinMax)
{
    return float4(
        float((packedMinMax.x >> 16u) & 0xffffu),
        float((packedMinMax.y >> 16u) & 0xffffu),
        float((packedMinMax.z >> 16u) & 0xffffu),
        float((packedMinMax.w >> 16u) & 0xffffu)) * (1.0f / 255.0f);
}

float4 PaletteColor(uint4 endpoint0, uint4 endpoint1, uint index)
{
    const uint weight = BC7Mode6Weights[index];
    const uint4 color = ((endpoint0 * (64u - weight)) + (endpoint1 * weight) + 32u) >> 6u;
    return float4(color) * (1.0f / 255.0f);
}

uint ChooseBestIndex(float4 sampleValue, uint4 endpoint0, uint4 endpoint1)
{
    uint bestIndex = 0u;
    float bestError = 3.402823466e+38F;

    [unroll]
    for (uint index = 0u; index < 16u; ++index)
    {
        const float4 palette = PaletteColor(endpoint0, endpoint1, index);
        const float4 delta = sampleValue - palette;
        const float error = dot(delta, delta);
        if (error < bestError)
        {
            bestError = error;
            bestIndex = index;
        }
    }

    return bestIndex;
}

BC7Mode6Block EncodeMode6(Texture2D<float4> srcTexture, uint2 blockBase, uint2 textureSize)
{
    uint4 alphaHigh;
    const uint4 packedMinMax = LoadBlockPixels(srcTexture, blockBase, textureSize, alphaHigh);
    const float4 endpointMin = DecodeMinEndpoint(packedMinMax);
    const float4 endpointMax = DecodeMaxEndpoint(packedMinMax);

    const uint2 endpoint0Quant = QuantizeEndpoint(endpointMin);
    const uint2 endpoint1Quant = QuantizeEndpoint(endpointMax);

    uint endpoint0Q7[4] = {
        endpoint0Quant.x & 0x7fu,
        (endpoint0Quant.x >> 8u) & 0x7fu,
        (endpoint0Quant.x >> 16u) & 0x7fu,
        (endpoint0Quant.x >> 24u) & 0x7fu
    };
    uint endpoint1Q7[4] = {
        endpoint1Quant.x & 0x7fu,
        (endpoint1Quant.x >> 8u) & 0x7fu,
        (endpoint1Quant.x >> 16u) & 0x7fu,
        (endpoint1Quant.x >> 24u) & 0x7fu
    };

    const uint endpoint0Pbit = endpoint0Quant.y;
    const uint endpoint1Pbit = endpoint1Quant.y;

    uint4 endpoint0 = uint4(
        ReconstructEndpoint(endpoint0Q7[0], endpoint0Pbit),
        ReconstructEndpoint(endpoint0Q7[1], endpoint0Pbit),
        ReconstructEndpoint(endpoint0Q7[2], endpoint0Pbit),
        ReconstructEndpoint(endpoint0Q7[3], endpoint0Pbit));
    uint4 endpoint1 = uint4(
        ReconstructEndpoint(endpoint1Q7[0], endpoint1Pbit),
        ReconstructEndpoint(endpoint1Q7[1], endpoint1Pbit),
        ReconstructEndpoint(endpoint1Q7[2], endpoint1Pbit),
        ReconstructEndpoint(endpoint1Q7[3], endpoint1Pbit));

    uint indices[16];
    [unroll]
    for (uint y = 0u; y < 4u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 4u; ++x)
        {
            const uint linearIndex = y * 4u + x;
            const uint2 pixel = min(blockBase + uint2(x, y), textureSize - 1u.xx);
            const float4 sampleValue = srcTexture.Load(int3(pixel, 0));
            indices[linearIndex] = ChooseBestIndex(sampleValue, endpoint0, endpoint1);
        }
    }

    if (indices[0] >= 8u)
    {
        const uint4 swappedEndpoint = endpoint0;
        endpoint0 = endpoint1;
        endpoint1 = swappedEndpoint;

        const uint swappedPbit = endpoint0Pbit;
        const uint endpoint0PbitLocal = endpoint1Pbit;
        const uint endpoint1PbitLocal = swappedPbit;

        endpoint0Q7[0] = (endpoint1.x >> 1u) & 0x7fu;
        endpoint0Q7[1] = (endpoint1.y >> 1u) & 0x7fu;
        endpoint0Q7[2] = (endpoint1.z >> 1u) & 0x7fu;
        endpoint0Q7[3] = (endpoint1.w >> 1u) & 0x7fu;
        endpoint1Q7[0] = (endpoint0.x >> 1u) & 0x7fu;
        endpoint1Q7[1] = (endpoint0.y >> 1u) & 0x7fu;
        endpoint1Q7[2] = (endpoint0.z >> 1u) & 0x7fu;
        endpoint1Q7[3] = (endpoint0.w >> 1u) & 0x7fu;

        [unroll]
        for (uint index = 0u; index < 16u; ++index)
        {
            indices[index] = 15u - indices[index];
        }

        BC7Mode6Block tempBlock = (BC7Mode6Block)0;
        tempBlock.bitPosition = 0u;
        WriteBits(tempBlock, 6u, 0u);
        WriteBits(tempBlock, 1u, 1u);

        [unroll]
        for (uint component = 0u; component < 4u; ++component)
        {
            WriteBits(tempBlock, 7u, endpoint0Q7[component]);
            WriteBits(tempBlock, 7u, endpoint1Q7[component]);
        }

        WriteBits(tempBlock, 1u, endpoint0PbitLocal);
        WriteBits(tempBlock, 1u, endpoint1PbitLocal);
        WriteBits(tempBlock, 3u, indices[0]);
        [unroll]
        for (uint index = 1u; index < 16u; ++index)
        {
            WriteBits(tempBlock, 4u, indices[index]);
        }
        return tempBlock;
    }

    BC7Mode6Block block = (BC7Mode6Block)0;
    block.bitPosition = 0u;
    WriteBits(block, 6u, 0u);
    WriteBits(block, 1u, 1u);

    [unroll]
    for (uint component = 0u; component < 4u; ++component)
    {
        WriteBits(block, 7u, endpoint0Q7[component]);
        WriteBits(block, 7u, endpoint1Q7[component]);
    }

    WriteBits(block, 1u, endpoint0Pbit);
    WriteBits(block, 1u, endpoint1Pbit);
    WriteBits(block, 3u, indices[0]);
    [unroll]
    for (uint index = 1u; index < 16u; ++index)
    {
        WriteBits(block, 4u, indices[index]);
    }

    return block;
}

[numthreads(8, 8, 1)]
void BC7CompressMode6CS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Texture2D<float4> srcTexture = ResourceDescriptorHeap[UintRootConstant0];
    RWByteAddressBuffer dstBuffer = ResourceDescriptorHeap[UintRootConstant1];

    const uint blocksX = (UintRootConstant4 + 3u) / 4u;
    const uint blocksY = (UintRootConstant5 + 3u) / 4u;
    if (dispatchThreadId.x >= blocksX || dispatchThreadId.y >= blocksY)
    {
        return;
    }

    const uint2 blockBase = dispatchThreadId.xy * 4u;
    const uint2 textureSize = uint2(UintRootConstant4, UintRootConstant5);
    BC7Mode6Block block = EncodeMode6(srcTexture, blockBase, textureSize);

    const uint byteOffset = UintRootConstant2 + dispatchThreadId.y * UintRootConstant3 + dispatchThreadId.x * 16u;
    dstBuffer.Store4(byteOffset, block.words);
}