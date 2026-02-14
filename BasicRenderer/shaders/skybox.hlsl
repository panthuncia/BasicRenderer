#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"

[shader("compute")]
[numthreads(8, 8, 1)]
void SkyboxCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint linearDepthMapSRVIndex = UintRootConstant0;
    const uint cameraBufferSRVIndex = UintRootConstant1;
    const uint environmentInfoSRVIndex = UintRootConstant2;
    const uint hdrTargetUAVIndex = UintRootConstant3;

    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    const uint screenW = perFrameBuffer.screenResX;
    const uint screenH = perFrameBuffer.screenResY;

    if (dispatchThreadId.x >= screenW || dispatchThreadId.y >= screenH)
    {
        return;
    }

    const uint2 pixel = dispatchThreadId.xy;

    Texture2D<float> linearDepthTexture = ResourceDescriptorHeap[linearDepthMapSRVIndex];
    const float linearDepth = linearDepthTexture[pixel];
    const float noGeometryDepth = asfloat(0x7F7FFFFF);

    if (linearDepth != noGeometryDepth)
    {
        return;
    }

    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferSRVIndex];
    const Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];

    StructuredBuffer<EnvironmentInfo> environmentInfo = ResourceDescriptorHeap[environmentInfoSRVIndex];
    const EnvironmentInfo envInfo = environmentInfo[perFrameBuffer.activeEnvironmentIndex];

    float2 uv = (float2(pixel) + 0.5f) / float2(screenW, screenH);
    uv.y = 1.0f - uv.y;
    const float2 ndc = uv * 2.0f - 1.0f;

    const float4 viewDirH = mul(float4(ndc, 1.0f, 1.0f), mainCamera.projectionInverse);
    const float3 viewDir = normalize(viewDirH.xyz / max(abs(viewDirH.w), 1e-6f));
    const float3 worldDir = normalize(mul(float4(viewDir, 0.0f), mainCamera.viewInverse).xyz);

    TextureCube<float4> skyboxTexture = ResourceDescriptorHeap[envInfo.cubeMapDescriptorIndex];
    const float3 color = skyboxTexture.SampleLevel(g_linearClamp, worldDir, 0.0f).xyz;

    RWTexture2D<float4> hdrTarget = ResourceDescriptorHeap[hdrTargetUAVIndex];
    hdrTarget[pixel] = float4(color, 1.0f);
}