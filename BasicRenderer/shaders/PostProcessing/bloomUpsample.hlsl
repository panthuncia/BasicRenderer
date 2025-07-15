#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "fullscreenVS.hlsli"
#include "include/gammaCorrection.hlsli"
#include "PerPassRootConstants/bloomSampleRootConstants.h"

float3 sample_for_upsample(Texture2D<float4> source, float2 texCoord, float x, float y)
{
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

// UintRootConstant2 is src res x
// UintRootConstant3 is src res y

// FloatRootConstant0 is filter radius
// FloatRootConstant1 is aspect ratio

float4 upsample(FULLSCREEN_VS_OUTPUT input) : SV_Target
{
    float filterRadius = FILTER_RADIUS;
    float x = filterRadius;
    float y = filterRadius * ASPECT_RATIO;
    
    Texture2D<float4> source = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostProcessing::UpscaledHDR)];
    float2 texCoord = input.uv;
    texCoord.y = 1.0f - texCoord.y;
    
    float3 upsample = sample_for_upsample(source, texCoord, x, y);
    
    return float4(upsample, 1.0);
}