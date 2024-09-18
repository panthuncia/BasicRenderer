// Vertex Shader
struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VS_OUTPUT VSMain(float3 pos : POSITION) {
    VS_OUTPUT output;
    output.position = float4(pos, 1.0f);
    output.uv = pos.xy * 0.5f + 0.5f; // Convert [-1, 1] to [0, 1] for UVs
    return output;
}

// Pixel Shader
Texture2D debugTexture : register(t0);
SamplerState samplerState : register(s0);

float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    return debugTexture.Sample(samplerState, input.uv);
}