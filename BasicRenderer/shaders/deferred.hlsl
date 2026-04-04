#include "include/vertex.hlsli"
#include "include/utilities.hlsli"
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/materialflags.hlsli"
#include "include/lighting.hlsli"
#include "include/gammaCorrection.hlsli"
#include "include/outputTypes.hlsli"
#include "include/debugPayload.hlsli"

[numthreads(8, 8, 1)]
void DeferredCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    uint screenW = perFrameBuffer.screenResX;
    uint screenH = perFrameBuffer.screenResY;

    if (dispatchThreadId.x >= screenW || dispatchThreadId.y >= screenH)
    {
        return;
    }
    
    uint2 pixel = dispatchThreadId.xy;
    float2 uv = (float2(pixel) + 0.5f) / float2(screenW, screenH);
    uv.y = 1.0f - uv.y;

    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    
    Texture2D<float> depthTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::LinearDepthMap)];
    float depth = depthTexture[pixel];
    if (asuint(depth) == 0x7F7FFFFF) // TODO: When we need more shading paths, we will move to prefix sum and indirect dispatch
    {
        // No geometry here
        return;
    }
    
    //float linearZ = unprojectDepth(depth, mainCamera.zNear, mainCamera.zFar);
    float linearZ = depth;

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
    
    // Always write lighting to HDR target
    hdrTarget[pixel] = float4(lighting, 1.0);

    // Write debug payload for lighting-derived modes
    if (perFrameBuffer.outputType != OUTPUT_COLOR) {
        RWTexture2D<uint2> debugVisTex = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::DebugVisualization)];
        uint2 payload = uint2(DEBUG_SENTINEL, DEBUG_SENTINEL);
        switch (perFrameBuffer.outputType) {
            case OUTPUT_AO:
                payload = PackDebugFloat3(fragmentInfo.diffuseAmbientOcclusion.xxx);
                break;
            case OUTPUT_DEPTH: {
                float scaledDepth = abs(linearZ) * 0.1;
                payload = PackDebugFloat3(scaledDepth.xxx);
                break;
            }
#if defined(PSO_IMAGE_BASED_LIGHTING)
            case OUTPUT_DIFFUSE_IBL:
                payload = PackDebugFloat3(lightingOutput.diffuseIBL.rgb);
                break;
            case OUTPUT_SPECULAR_IBL:
                payload = PackDebugFloat3(lightingOutput.specularIBL.rgb);
                break;
#endif
#if defined(PSO_CLUSTERED_LIGHTING)
            case OUTPUT_LIGHT_CLUSTER_ID:
                payload = PackDebugUint(lightingOutput.clusterIndex);
                break;
            case OUTPUT_LIGHT_CLUSTER_LIGHT_COUNT:
                payload = PackDebugUint(lightingOutput.clusterLightCount);
                break;
            case OUTPUT_VSM_PREFERRED_CLIPMAP:
            case OUTPUT_VSM_SAMPLED_CLIPMAP:
            case OUTPUT_VSM_PAGE_STATE:
            case OUTPUT_VSM_PHYSICAL_PAGE:
                payload = lightingOutput.shadowDebugPayload;
                break;
#endif
        }
        if (payload.x != DEBUG_SENTINEL) {
            WriteDebugPixel(debugVisTex, pixel, payload);
        }
    }
}