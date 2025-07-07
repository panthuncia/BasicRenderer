#ifndef __LOADING_UTILITY_HLSL__
#define __LOADING_UTILITY_HLSL__

// Helper function to load a float3 from a ByteAddressBuffer
float3 LoadFloat3(uint offset, ByteAddressBuffer buffer) {
    float3 result;
    result.x = asfloat(buffer.Load(offset));
    result.y = asfloat(buffer.Load(offset + 4));
    result.z = asfloat(buffer.Load(offset + 8));
    return result;
}

// Helper function to load a float2 from a ByteAddressBuffer
float2 LoadFloat2(uint offset, ByteAddressBuffer buffer) {
    float2 result;
    result.x = asfloat(buffer.Load(offset));
    result.y = asfloat(buffer.Load(offset + 4));
    return result;
}

// Helper function to load a uint4 from a ByteAddressBuffer
uint4 LoadUint4(uint offset, ByteAddressBuffer buffer) {
    uint4 result;
    result.x = buffer.Load(offset);
    result.y = buffer.Load(offset + 4);
    result.z = buffer.Load(offset + 8);
    result.w = buffer.Load(offset + 12);
    return result;
}

// Helper function to load a float4 from a ByteAddressBuffer
float4 LoadFloat4(uint offset, ByteAddressBuffer buffer) {
    float4 result;
    result.x = asfloat(buffer.Load(offset));
    result.y = asfloat(buffer.Load(offset + 4));
    result.z = asfloat(buffer.Load(offset + 8));
    result.w = asfloat(buffer.Load(offset + 12));
    return result;
}

#define GET_BIT(bits, idx)((bits.Load( ((idx) >> 5) << 2 ) >> ( (idx) & 31 ) ) & 1u)

uint GetBit(ByteAddressBuffer bits, uint bitIndex)
{
    uint wordOff = (bitIndex >> 5) << 2;
    uint word = bits.Load(wordOff);
    return (word >> (bitIndex & 31)) & 1u;
}

uint GetBit(RWByteAddressBuffer bits, uint bitIndex)
{
    uint wordOff = (bitIndex >> 5) << 2;
    uint word = bits.Load(wordOff);
    return (word >> (bitIndex & 31)) & 1u;
}

void WriteBit(RWByteAddressBuffer bits, uint bitIndex, uint value)
{
    uint wordOff = (bitIndex >> 5) << 2; // word index * 4
    uint bitInWord = bitIndex & 31;
    uint mask = 1u << bitInWord;

    // load-modify-store
    uint word = bits.Load(wordOff);
    word = (word & ~mask)
         | ((value & 1u) << bitInWord);
    bits.Store(wordOff, word);
}


void SetBitAtomic(RWByteAddressBuffer bits, uint bitIndex)
{
    uint wordOff = (bitIndex >> 5) << 2;
    uint bitInWord = bitIndex & 31;
    uint mask = 1u << bitInWord;

    uint old;
    bits.InterlockedOr(wordOff, mask, old);
}

void ClearBitAtomic(RWByteAddressBuffer bits, uint bitIndex)
{
    uint wordOff = (bitIndex >> 5) << 2;
    uint bitInWord = bitIndex & 31;
    uint mask = ~(1u << bitInWord);

    uint old;
    bits.InterlockedAnd(wordOff, mask, old);
}

#endif // __LOADING_UTILITY_HLSL__