#include "include/structs.hlsli"

// point-clamp at s0
SamplerState g_pointClamp : register(s0);

// linear-clamp at s1
SamplerState g_linearClamp : register(s1);

cbuffer RootConstants1 : register(b0)
{
    matrix viewProjectionMatrix;
};

cbuffer RootConstants2 : register(b1)
{
    uint environmentBufferDescriptorIndex;
};

// Vertex Shader
struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD1;
};

VS_OUTPUT VSMain(float3 pos : POSITION)
{
    VS_OUTPUT output;
    output.direction = normalize(pos);
    output.position = mul(viewProjectionMatrix, float4(pos, 1.0f));
    output.position.z = output.position.w - 0.00001;
    return output;
}

// Pixel Shader

float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<EnvironmentInfo> environmentInfo = ResourceDescriptorHeap[environmentBufferDescriptorIndex];
    EnvironmentInfo envInfo = environmentInfo[perFrameBuffer.activeEnvironmentIndex];
    TextureCube<float4> skyboxTexture = ResourceDescriptorHeap[envInfo.cubeMapDescriptorIndex];
    float3 color = skyboxTexture.Sample(g_linearClamp, input.direction.xyz).xyz;
    
    return float4(color, 1.0);
}