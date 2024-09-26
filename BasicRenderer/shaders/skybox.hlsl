cbuffer RootConstants1 : register(b1) {
    matrix viewProjectionMatrix;
};

cbuffer RootConstants2 : register(b2) {
    uint skyboxTextureIndex;
};

cbuffer RootConstants1 : register(b3) {
    uint skyboxSamplerIndex;
};

// Vertex Shader
struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD1;
};

VS_OUTPUT VSMain(float3 pos : POSITION) {
    VS_OUTPUT output;
    output.direction = pos;
    output.position = mul(viewProjectionMatrix, float4(pos, 1.0f));
    output.position.z = output.position.w-0.00001;
    return output;
}

// Pixel Shader

float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    TextureCube<float4> skyboxTexture = ResourceDescriptorHeap[skyboxTextureIndex];
    SamplerState skyboxSampler = SamplerDescriptorHeap[skyboxSamplerIndex];
    float3 color = skyboxTexture.Sample(skyboxSampler, input.direction.xyz).xyz;
    return float4(color, 1.0);
}