
cbuffer PerFrame : register(b0) {
    row_major matrix view;
    row_major matrix projection;
};

cbuffer PerMesh : register(b1)
{
    row_major matrix model;
};

struct VSInput {
    float3 position : POSITION;
    float4 color : COLOR;
};

struct PSInput {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    float4 worldPosition = mul(float4(input.position, 1.0f), model);
    float4 viewPosition = mul(worldPosition, view);
    output.position = mul(viewPosition, projection);
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET {
    return input.color;
}