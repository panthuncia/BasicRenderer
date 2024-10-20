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

matrix loadMatrixFromBuffer(StructuredBuffer<float4> matrixBuffer, uint matrixIndex) {
    float4 bone1Row1 = matrixBuffer[matrixIndex * 4];
    float4 bone1Row2 = matrixBuffer[matrixIndex * 4 + 1];
    float4 bone1Row3 = matrixBuffer[matrixIndex * 4 + 2];
    float4 bone1Row4 = matrixBuffer[matrixIndex * 4 + 3];
    return float4x4(bone1Row1, bone1Row2, bone1Row3, bone1Row4);
}