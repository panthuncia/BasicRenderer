#ifndef __FULLSCREEN_VS_HLSLI__
#define __FULLSCREEN_VS_HLSLI__

struct FULLSCREEN_VS_OUTPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD1;
};
FULLSCREEN_VS_OUTPUT VSMain(float3 pos : POSITION, float2 uv : TEXCOORD0)
{
    FULLSCREEN_VS_OUTPUT output;
    output.position = float4(pos, 1.0f);
    output.uv = uv;
    return output;
}

#endif // __FULLSCREEN_VS_HLSLI__