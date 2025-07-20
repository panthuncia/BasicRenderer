#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "fullscreenVS.hlsli"
#include "include/gammaCorrection.hlsli"
#include "PerPassRootConstants/bloomSampleRootConstants.h"

// UintRootConstant0 is HDR source SRV
// UintRootConstant1 is mip level of bloom source SRV
// UintRootConstant2 is src res x
// UintRootConstant3 is src res y

// FloatRootConstant0 is src texel size x
// FloatRootConstant1 is src texel size y
float4 downsample(FULLSCREEN_VS_OUTPUT input) : SV_Target
{
    float x = SRC_TEXEL_SIZE_X;
    float y = SRC_TEXEL_SIZE_Y;
    
    Texture2D<float4> source = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostProcessing::UpscaledHDR)];
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
    
    downsample = max(downsample, 0.00001f);
    
    if (length(downsample) < 1.0)
        return (float4(0, 0, 0, 1));
    
    return float4(downsample, 1.0);
}