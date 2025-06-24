#include "cbuffers.hlsli"
#include "structs.hlsli"
#include "fullscreenVS.hlsli"
#include "IBL.hlsli"
#include "utilities.hlsli"

// UintRootConstant0 is screen-space reflection SRV
float4 PSMain(FULLSCREEN_VS_OUTPUT input) : SV_Target
{
    float2 texCoord = input.uv;
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    uint2 screenRes = float2(perFrameBuffer.screenResX, perFrameBuffer.screenResY);
    texCoord.y = 1.0f - texCoord.y;

    uint2 screenAddress = (uint2) (texCoord * screenRes);
    
    Texture2D<float4> screenSpaceReflection = ResourceDescriptorHeap[UintRootConstant0];
    float4 reflectionColor = screenSpaceReflection[screenAddress];
    
    FragmentInfo fragmentInfo;
    float3 _ = float3(0, 0, 0);
    float3 __ = float3(0, 0, 0);
    float3 ___ = float3(0, 0, 0); // TODO: Create a variant for when we don't need position info
    GetFragmentInfoScreenSpace(input.position.xy, _, __, ___, enableGTAO, fragmentInfo);
    
    float3 specularIBL = evaluateSpecularIBLFromSSR(reflectionColor.rgb, fragmentInfo.normalWS, fragmentInfo.normalWS, fragmentInfo.diffuseAmbientOcclusion, fragmentInfo.F0, fragmentInfo.roughness, fragmentInfo.perceptualRoughness, fragmentInfo.NdotV);
    return float4(specularIBL, 1.0);
}