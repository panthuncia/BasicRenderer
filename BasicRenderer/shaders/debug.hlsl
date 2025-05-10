
cbuffer RootConstants1 : register(b0) {
    matrix viewMatrix;
};

// Vertex Shader
struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD1;
};

VS_OUTPUT VSMain(float3 pos : POSITION, float2 uv : TEXCOORD0) {
    VS_OUTPUT output;
    output.position = mul(float4(pos, 1.0f), viewMatrix);
    output.uv = uv;
    return output;
}

// Pixel Shader
Texture2DArray<float> debugTexture : register(t0);
SamplerState samplerState : register(s0);

float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    return float4(debugTexture.SampleLevel(samplerState, float3(input.uv, 2), 0), 0.0, 0.0, 1.0);
}