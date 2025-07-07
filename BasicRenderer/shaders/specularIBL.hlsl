#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "fullscreenVS.hlsli"
#include "include/IBL.hlsli"
#include "include/utilities.hlsli"

// UintRootConstant0 is screen-space reflection SRV
// UintRootConstan1 is depth texture SRV
float4 PSMain(FULLSCREEN_VS_OUTPUT input) : SV_Target
{
    float2 texCoord = input.uv;
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    uint2 screenRes = float2(perFrameBuffer.screenResX, perFrameBuffer.screenResY);
    texCoord.y = 1.0f - texCoord.y;

    uint2 screenAddress = (uint2) (texCoord * screenRes);
    
    Texture2D<float4> screenSpaceReflection = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostProcessing::ScreenSpaceReflections)];
    float3 reflectionColor = screenSpaceReflection[screenAddress].xyz;
    
    // Basically another deferred pass
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    
    Texture2D<float> depthTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::DepthTexture)];
    float depth = depthTexture[input.position.xy];
    
    float linearZ = unprojectDepth(depth, mainCamera.zNear, mainCamera.zFar);
    
    float2 pixel = input.position.xy;
    //float2 uv = (pixel) / float2(perFrameBuffer.screenResX, perFrameBuffer.screenResY); // [0,1] over screen
    float2 ndc = input.uv * 2.0f - 1.0f;
    
    float4 clipPos = float4(ndc, 1.0f, 1.0f);

    // unproject back into view space:
    float4 viewPosH = mul(clipPos, mainCamera.projectionInverse);

    float3 positionVS = viewPosH.xyz * linearZ;
    //positionVS.y = -positionVS.y;
    
    float4 worldPosH = mul(float4(positionVS, 1.0f), mainCamera.viewInverse);
    float3 positionWS = worldPosH.xyz;
    
    float3 viewDirWS = normalize(mainCamera.positionWorldSpace.xyz - positionWS.xyz);
    
    FragmentInfo fragmentInfo;
    GetFragmentInfoScreenSpace(input.position.xy, viewDirWS, positionVS, positionWS, enableGTAO, fragmentInfo);
    
    float3 specularIBL = evaluateSpecularIBLFromSSR(reflectionColor.rgb, fragmentInfo.normalWS, fragmentInfo.normalWS, fragmentInfo.diffuseAmbientOcclusion, fragmentInfo.F0, fragmentInfo.roughness, fragmentInfo.perceptualRoughness, fragmentInfo.NdotV);
    return float4(specularIBL, 1.0);
}