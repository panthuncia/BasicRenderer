#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "fullscreenVS.hlsli"
#include "include/gammaCorrection.hlsli"
#include "PerPassRootConstants/bloomBlendRootConstants.h"

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
void blend(FULLSCREEN_VS_OUTPUT input) : SV_Target
{
    float filterRadius = FILTER_RADIUS;
    float x = filterRadius;
    float y = filterRadius * ASPECT_RATIO;
    
    RWTexture2D<float4> HDR = ResourceDescriptorHeap[HDR_TARGET_UAV_DESCRIPTOR_INDEX];
    Texture2D<float4> bloom = ResourceDescriptorHeap[BLOOM_SOURCE_SRV_DESCRIPTOR_INDEX];
    float2 texCoord = input.uv;
    texCoord.y = 1.0f - texCoord.y;

    float3 finalBloom = sample_for_upsample(bloom, texCoord, x, y);
    
    uint2 screenAddress = (uint2) (texCoord * uint2(DST_WIDTH, DST_HEIGHT));
    
    float3 existing = HDR[screenAddress].rgb;
    HDR[screenAddress].rgb = lerp(existing, finalBloom, 0.04);
}