#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "fullscreenVS.hlsli"
#include "include/gammaCorrection.hlsli"

// UintRootConstant0 is HDR source SRV
// UintRootConstant1 is mip level of bloom source SRV
// UintRootConstant2 is src res x
// UintRootConstant3 is src res y

// FloatRootConstant0 is src texel size x
// FloatRootConstant1 is src texel size y
float4 downsample(FULLSCREEN_VS_OUTPUT input) : SV_Target
{
    float x = FloatRootConstant0;
    float y = FloatRootConstant1;
    
    Texture2D<float4> source = ResourceDescriptorHeap[UintRootConstant0];
    float2 texCoord = input.uv;
    texCoord.y = 1.0f - texCoord.y;
    
    float3 a = source.SampleLevel(g_linearClamp, float2(texCoord.x - 2 * x, texCoord.y + 2 * y), 0).rgb;
    float3 b = source.SampleLevel(g_linearClamp, float2(texCoord.x, texCoord.y + 2 * y), 0).rgb;
    float3 c = source.SampleLevel(g_linearClamp, float2(texCoord.x + 2 * x, texCoord.y + 2 * y), 0).rgb;
    
    float3 d = source.SampleLevel(g_linearClamp, float2(texCoord.x - 2 * x, texCoord.y), 0).rgb;
    float3 e = source.SampleLevel(g_linearClamp, float2(texCoord.x, texCoord.y), 0).rgb;
    float3 f = source.SampleLevel(g_linearClamp, float2(texCoord.x + 2 * x, texCoord.y), 0).rgb;
    
    float3 g = source.SampleLevel(g_linearClamp, float2(texCoord.x - 2 * x, texCoord.y - 2 * y), 0).rgb;
    float3 h = source.SampleLevel(g_linearClamp, float2(texCoord.x, texCoord.y - 2 * y), 0).rgb;
    float3 i = source.SampleLevel(g_linearClamp, float2(texCoord.x + 2 * x, texCoord.y - 2 * y), 0).rgb;
    
    float3 j = source.SampleLevel(g_linearClamp, float2(texCoord.x - x, texCoord.y + y), 0).rgb;
    float3 k = source.SampleLevel(g_linearClamp, float2(texCoord.x + x, texCoord.y + y), 0).rgb;
    float3 l = source.SampleLevel(g_linearClamp, float2(texCoord.x - x, texCoord.y - y), 0).rgb;
    float3 m = source.SampleLevel(g_linearClamp, float2(texCoord.x + x, texCoord.y - y), 0).rgb;
    
    float3 downsample = e * 0.125;
    downsample += (a + c + g + i) * 0.03125;
    downsample += (b + d + f + h) * 0.0625;
    downsample += (j + k + l + m) * 0.125;
    
    downsample = max(downsample, 0.0001f);
    
    return float4(downsample, 1.0);
}

float3 sample_for_upsample(Texture2D<float4> source, float2 texCoord, float x, float y) {
    float3 a = source.SampleLevel(g_linearClamp, float2(texCoord.x - x, texCoord.y + y), 0).rgb;
    float3 b = source.SampleLevel(g_linearClamp, float2(texCoord.x, texCoord.y + y), 0).rgb;
    float3 c = source.SampleLevel(g_linearClamp, float2(texCoord.x + x, texCoord.y + y), 0).rgb;
    
    float3 d = source.SampleLevel(g_linearClamp, float2(texCoord.x - x, texCoord.y), 0).rgb;
    float3 e = source.SampleLevel(g_linearClamp, float2(texCoord.x, texCoord.y), 0).rgb;
    float3 f = source.SampleLevel(g_linearClamp, float2(texCoord.x + x, texCoord.y), 0).rgb;
    
    float3 g = source.SampleLevel(g_linearClamp, float2(texCoord.x - x, texCoord.y - y), 0).rgb;
    float3 h = source.SampleLevel(g_linearClamp, float2(texCoord.x, texCoord.y - y), 0).rgb;
    float3 i = source.SampleLevel(g_linearClamp, float2(texCoord.x + x, texCoord.y - y), 0).rgb;
    
    float3 upsample = e * 4.0;
    upsample += (b + d + f + h) * 2.0;
    upsample += (a + c + g + i);
    upsample *= 1.0 / 16.0;
    return upsample;
}

// UintRootConstant0 is HDR target UAV
// UintRootConstant1 is bloom source SRV
// UintRootConstant2 is src res x
// UintRootConstant3 is src res y

// FloatRootConstant0 is filter radius
// FloatRootConstant1 is aspect ratio

float4 upsample(FULLSCREEN_VS_OUTPUT input) : SV_Target
{
    float filterRadius = FloatRootConstant0;
    float x = filterRadius;
    float y = filterRadius * FloatRootConstant1;
    
    Texture2D<float4> source = ResourceDescriptorHeap[UintRootConstant0];
    float2 texCoord = input.uv;
    texCoord.y = 1.0f - texCoord.y;
    
    float3 upsample = sample_for_upsample(source, texCoord, x, y);
    
    return float4(upsample, 1.0);
}

// UintRootConstant0 is HDR target UAV
// UintRootConstant1 is bloom source SRV
// UintRootConstant2 is src res x
// UintRootConstant3 is src res y

// FloatRootConstant0 is filter radius
// FloatRootConstant1 is aspect ratio
void blend(FULLSCREEN_VS_OUTPUT input) : SV_Target
{
    float filterRadius = FloatRootConstant0;
    float x = filterRadius;
    float y = filterRadius * FloatRootConstant1;
    
    RWTexture2D<float4> HDR = ResourceDescriptorHeap[UintRootConstant0];
    Texture2D<float4> bloom = ResourceDescriptorHeap[UintRootConstant1];
    float2 texCoord = input.uv;
    texCoord.y = 1.0f - texCoord.y;

    float3 finalBloom = sample_for_upsample(bloom, texCoord, x, y);
    
    uint2 screenAddress = (uint2) (texCoord * uint2(UintRootConstant2, UintRootConstant3));
    
    float3 existing = HDR[screenAddress].rgb;
    HDR[screenAddress].rgb = lerp(existing, finalBloom, 0.04);
}