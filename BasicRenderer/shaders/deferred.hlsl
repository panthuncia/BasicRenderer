#include "include/vertex.hlsli"
#include "include/utilities.hlsli"
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/materialflags.hlsli"
#include "include/lighting.hlsli"
#include "include/gammaCorrection.hlsli"
#include "include/outputTypes.hlsli"

[numthreads(8, 8, 1)]
void DeferredCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    uint screenW = perFrameBuffer.screenResX;
    uint screenH = perFrameBuffer.screenResY;

    if (dispatchThreadId.x >= screenW || dispatchThreadId.y >= screenH)
        return;

    uint2 pixel = dispatchThreadId.xy;
    float2 uv = (float2(pixel) + 0.5f) / float2(screenW, screenH);
    uv.y = 1.0f - uv.y;

    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    
    Texture2D<float> depthTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::DepthTexture)];
    float depth = depthTexture[pixel];
    if (depth == 1.0f) // TODO: When we need more material paths, we will move to prefix sum and indirect dispatch
    {
        // No geometry here
        return;
    }
    
    float linearZ = unprojectDepth(depth, mainCamera.zNear, mainCamera.zFar);

    float2 ndc = uv * 2.0f - 1.0f;
    float4 clipPos = float4(ndc, 1.0f, 1.0f);
    float4 viewPosH = mul(clipPos, mainCamera.projectionInverse);
    float3 positionVS = viewPosH.xyz * linearZ;

    float4 worldPosH = mul(float4(positionVS, 1.0f), mainCamera.viewInverse);
    float3 positionWS = worldPosH.xyz;

    float3 viewDirWS = normalize(mainCamera.positionWorldSpace.xyz - positionWS);

    FragmentInfo fragmentInfo;
    GetFragmentInfoScreenSpace(pixel, viewDirWS, positionVS, positionWS, enableGTAO, fragmentInfo);
    
    LightingOutput lightingOutput = lightFragment(fragmentInfo, mainCamera, perFrameBuffer.activeEnvironmentIndex, ResourceDescriptorIndex(Builtin::Environment::InfoBuffer), true);
    
    float3 lighting = lightingOutput.lighting;

    RWTexture2D<float4> hdrTarget = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Color::HDRColorTarget)];
    hdrTarget[pixel] = float4(1.0, 0, 0, 1.0);
    
    float4 outColor;
    switch (perFrameBuffer.outputType)
    {
        case OUTPUT_COLOR:
            outColor = float4(lighting, 1.0);
            break;
        case OUTPUT_NORMAL:
            outColor = float4(fragmentInfo.normalWS * 0.5 + 0.5, 1.0);
            break;
        case OUTPUT_ALBEDO:
            outColor = float4(fragmentInfo.albedo.rgb, 1.0);
            break;
        case OUTPUT_METALLIC:
            outColor = float4(fragmentInfo.metallic.xxx, 1.0);
            break;
        case OUTPUT_ROUGHNESS:
            outColor = float4(fragmentInfo.roughness.xxx, 1.0);
            break;
        case OUTPUT_EMISSIVE:
            outColor = float4(fragmentInfo.emissive.rgb, 1.0);
            break;
        case OUTPUT_AO:
            outColor = float4(fragmentInfo.diffuseAmbientOcclusion.xxx, 1.0);
            break;
        case OUTPUT_DEPTH:{
                float scaledDepth = abs(linearZ) * 0.1;
                outColor = float4(scaledDepth.xxx, 1.0);
                break;
            }
#if defined(PSO_IMAGE_BASED_LIGHTING)
        case OUTPUT_DIFFUSE_IBL:      outColor = float4(lightingOutput.diffuseIBL.rgb, 1.0); break;
        case OUTPUT_SPECULAR_IBL:     outColor = float4(lightingOutput.specularIBL.rgb, 1.0); break;
#endif
        default:
            outColor = float4(1, 0, 0, 1);
            break;
    }

    hdrTarget[pixel] = outColor;
}