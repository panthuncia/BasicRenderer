#include "include/clodResolveCommon.hlsli"
#include "include/debugPayload.hlsli"

void EvaluateGBufferOptimized(uint2 pixel)
{
    Texture2D<uint64_t> visibilityTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibilityTexture)];
    uint64_t vis = visibilityTexture[pixel];

    ClodResolvedSample sample;
    if (!ResolveClodSampleFromVisKey(vis, pixel, sample))
    {
        return;
    }

    RWTexture2D<float4> normalsTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Normals)];
    RWTexture2D<float4> albedoTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Albedo)];
    RWTexture2D<float4> emissiveTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Emissive)];
    RWTexture2D<float2> metallicRoughnessTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::MetallicRoughness)];
    RWTexture2D<float2> motionVectorTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::MotionVectors)];

    normalsTexture[pixel].xyz = sample.materialInputs.normalWS;
    albedoTexture[pixel] = float4(sample.materialInputs.albedo, sample.materialInputs.ambientOcclusion);
    emissiveTexture[pixel].xyz = sample.materialInputs.emissive;
    metallicRoughnessTexture[pixel] = float2(sample.materialInputs.metallic, sample.materialInputs.roughness);
    motionVectorTexture[pixel] = sample.motionVector;

    ConstantBuffer<PerFrameBuffer> perFrame = ResourceDescriptorHeap[0];
    uint outputType = perFrame.outputType;
    if (outputType != OUTPUT_COLOR) {
        RWTexture2D<uint2> debugVisTex = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::DebugVisualization)];
        uint2 payload = uint2(DEBUG_SENTINEL, DEBUG_SENTINEL);
        switch (outputType) {
            case OUTPUT_NORMAL:
                payload = PackDebugFloat3(sample.materialInputs.normalWS * 0.5 + 0.5);
                break;
            case OUTPUT_ALBEDO:
                payload = PackDebugFloat3(sample.materialInputs.albedo);
                break;
            case OUTPUT_METALLIC:
                payload = PackDebugFloat3(sample.materialInputs.metallic.xxx);
                break;
            case OUTPUT_ROUGHNESS:
                payload = PackDebugFloat3(sample.materialInputs.roughness.xxx);
                break;
            case OUTPUT_EMISSIVE:
                payload = PackDebugFloat3(sample.materialInputs.emissive);
                break;
            case OUTPUT_AO:
                payload = PackDebugFloat3(sample.materialInputs.ambientOcclusion.xxx);
                break;
            case OUTPUT_MESHLETS:
                payload = PackDebugUint(sample.meshletIndex);
                break;
            case OUTPUT_MODEL_NORMALS:
                payload = PackDebugFloat3(sample.normalOS * 0.5 + 0.5);
                break;
            case OUTPUT_MOTION_VECTORS:
                payload = PackDebugFloat3(float3(sample.motionVector * 0.5 + 0.5, 0.5));
                break;
        }
        if (payload.x != DEBUG_SENTINEL) {
            WriteDebugPixel(debugVisTex, pixel, payload);
        }
    }
}

[numthreads(8, 8, 1)]
void PerViewPrimaryDepthCopyCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint screenW = UintRootConstant2;
    uint screenH = UintRootConstant3;

    if (dispatchThreadId.x >= screenW || dispatchThreadId.y >= screenH)
    {
        return;
    }

    uint2 pixel = dispatchThreadId.xy;
    Texture2D<uint64_t> visibilityTexture = ResourceDescriptorHeap[UintRootConstant0];
    uint64_t vis = visibilityTexture[pixel];

    float depth;
    if (vis == 0xFFFFFFFFFFFFFFFF)
    {
        depth = asfloat(0x7F7FFFFF);
    }
    else
    {
        uint clusterIndex;
        uint meshletTriangleIndex;
        UnpackVisKey(vis, depth, clusterIndex, meshletTriangleIndex);
    }

    RWTexture2D<float> linearDepthTexture = ResourceDescriptorHeap[UintRootConstant1];
    linearDepthTexture[pixel] = depth;

    uint projectedDepthUAVIndex = UintRootConstant4;
    if (projectedDepthUAVIndex != 0xFFFFFFFFu)
    {
        float projectedDepth;
        if (vis == 0xFFFFFFFFFFFFFFFF)
        {
            projectedDepth = 0.0f;
        }
        else
        {
            float M22 = FloatRootConstant0;
            float M32 = FloatRootConstant1;
            projectedDepth = -M22 + M32 / depth;
        }
        RWTexture2D<float> projectedDepthTexture = ResourceDescriptorHeap[projectedDepthUAVIndex];
        projectedDepthTexture[pixel] = projectedDepth;
    }
}

[numthreads(8, 8, 1)]
void PrimaryDepthCopyCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    uint screenW = perFrameBuffer.screenResX;
    uint screenH = perFrameBuffer.screenResY;

    if (dispatchThreadId.x >= screenW || dispatchThreadId.y >= screenH)
    {
        return;
    }

    uint2 pixel = dispatchThreadId.xy;
    Texture2D<uint64_t> visibilityTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibilityTexture)];

    uint64_t vis = visibilityTexture[pixel];
    float depth;
    if (vis == 0xFFFFFFFFFFFFFFFF)
    {
        depth = asfloat(0x7F7FFFFF);
    }
    else
    {
        uint clusterIndex;
        uint meshletTriangleIndex;
        UnpackVisKey(vis, depth, clusterIndex, meshletTriangleIndex);
    }

    RWTexture2D<float> linearDepthTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::LinearDepthMap)];
    linearDepthTexture[pixel] = depth;
}
