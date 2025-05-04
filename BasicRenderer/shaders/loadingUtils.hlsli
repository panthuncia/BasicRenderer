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

uint GetBit(ByteAddressBuffer bits, uint bitIndex)
{
    // which 32-bit word, and byte offset in the buffer
    uint wordIdx = bitIndex >> 5; // divide by 32
    uint byteOff = wordIdx * 4; // 4 bytes per 32-bit word
    uint bitInWord = bitIndex & 31; // mod 32

    uint word = bits.Load(byteOff);
    return (word >> bitInWord) & 1u;
}

void WriteBit(RWByteAddressBuffer bits, uint bitIndex, uint value)
{
    uint wordIdx = bitIndex >> 5;
    uint byteOff = wordIdx * 4;
    uint bitInWord = bitIndex & 31;
    uint mask = 1u << bitInWord;

    // load-modify-store
    uint word = bits.Load(byteOff);
    word = (word & ~mask) // clear that bit
         | ((value & 1u) << bitInWord); // set it to (value & 1)
    bits.Store(byteOff, word);
}


void SetBitAtomic(RWByteAddressBuffer bits, uint bitIndex)
{
    uint wordIdx = bitIndex >> 5;
    uint byteOff = wordIdx * 4;
    uint bitInWord = bitIndex & 31;
    uint mask = 1u << bitInWord;

    // atomically OR in the mask
    uint old;
    bits.InterlockedOr(byteOff, mask, old);
}

void ClearBitAtomic(RWByteAddressBuffer bits, uint bitIndex)
{
    uint wordIdx = bitIndex >> 5;
    uint byteOff = wordIdx * 4;
    uint bitInWord = bitIndex & 31;
    uint mask = ~(1u << bitInWord);

    // atomically AND with the inverse mask
    uint old;
    bits.InterlockedAnd(byteOff, mask, old);
}

#endif // __LOADING_UTILITY_HLSL__