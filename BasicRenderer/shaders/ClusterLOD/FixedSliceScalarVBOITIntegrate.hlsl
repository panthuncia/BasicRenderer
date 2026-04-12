#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/outputTypes.hlsli"
#include "include/debugPayload.hlsli"
#include "include/clodResolveCommon.hlsli"
#include "PerPassRootConstants/visUtilRootConstants.h"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITIntegrateRootConstants.h"

#define CLOD_DEEP_VISIBILITY_LIST_NULL 0xffffffffu

bool IsFixedSliceScalarVBOITDebugOutput(uint outputType)
{
    return outputType == OUTPUT_TRANSPARENT_VBOIT_TRANSMITTANCE;
}

uint ComputeDepthSlice(CLodFixedSliceScalarVBOITConfig config, float depth)
{
    const float depthRange = max(config.viewFarDepth - config.viewNearDepth, 1.0e-5f);
    const float normalizedDepth = saturate((depth - config.viewNearDepth) / depthRange);
    return min(config.sliceCount - 1u, (uint)(normalizedDepth * config.sliceCount));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodFixedSliceScalarVBOITIntegrateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerFrameBuffer)];
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_CONFIG_DESCRIPTOR_INDEX];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];

    if (config.sliceCount == 0u ||
        dispatchThreadId.x >= config.lowResolutionWidth ||
        dispatchThreadId.y >= config.lowResolutionHeight ||
        config.occupancyUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.extinctionUAVDescriptorIndex == 0xFFFFFFFFu ||
        config.integratedTransmittanceUAVDescriptorIndex == 0xFFFFFFFFu)
    {
        return;
    }

    Texture2D<uint> headPointers = ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_HEAD_POINTER_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodDeepVisibilityNode> nodeBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_INTEGRATE_NODE_BUFFER_DESCRIPTOR_INDEX];
    RWTexture2D<float> occupancyTexture = ResourceDescriptorHeap[config.occupancyUAVDescriptorIndex];
    RWTexture2DArray<float> extinctionTexture = ResourceDescriptorHeap[config.extinctionUAVDescriptorIndex];
    RWTexture2DArray<float> integratedTransmittanceTexture = ResourceDescriptorHeap[config.integratedTransmittanceUAVDescriptorIndex];

    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    const uint2 lowPixel = dispatchThreadId.xy;
    const uint2 renderResolution = uint2(perFrameBuffer.screenResX, perFrameBuffer.screenResY);
    const uint2 tileMin = lowPixel * CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR;
    const uint2 tileMax = min(
        tileMin + uint2(CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR, CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_DOWNSAMPLE_FACTOR),
        renderResolution);

    float sliceTransmittance[CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_SLICE_COUNT];
    [unroll]
    for (uint sliceIndex = 0u; sliceIndex < CLOD_FIXED_SLICE_SCALAR_VBOIT_DEFAULT_SLICE_COUNT; ++sliceIndex)
    {
        sliceTransmittance[sliceIndex] = 1.0f;
    }

    [loop]
    for (uint fullY = tileMin.y; fullY < tileMax.y; ++fullY)
    {
        [loop]
        for (uint fullX = tileMin.x; fullX < tileMax.x; ++fullX)
        {
            uint current = headPointers[uint2(fullX, fullY)];
            [loop]
            while (current != CLOD_DEEP_VISIBILITY_LIST_NULL)
            {
                CLodDeepVisibilityNode node = nodeBuffer[current];
                const bool isBackface = (node.flags & 0x1u) != 0u;

                ClodResolvedSample sample;
                if (ResolveClodSampleFromVisKeyWithFace(node.visKey, uint2(fullX, fullY), isBackface, sample))
                {
                    FragmentInfo fragmentInfo = (FragmentInfo)0;
                    fragmentInfo.pixelCoords = float2(fullX, fullY);
                    fragmentInfo.fragPosWorldSpace = sample.positionWS;
                    fragmentInfo.fragPosViewSpace = sample.positionVS;

                    const float3 viewWS = normalize(mainCamera.positionWorldSpace.xyz - sample.positionWS);
                    FillFragmentInfoDirect(
                        fragmentInfo,
                        sample.materialInputs,
                        viewWS,
                        float2(fullX, fullY),
                        true,
                        !isBackface,
                        sample.materialFlags);

                    const float alpha = saturate(fragmentInfo.alpha);
                    if (alpha > 0.0f)
                    {
                        const uint sliceIndex = ComputeDepthSlice(config, sample.linearDepth);
                        sliceTransmittance[sliceIndex] *= (1.0f - alpha);
                    }
                }

                current = node.next;
            }
        }
    }

    float cumulativeTransmittance = 1.0f;
    [loop]
    for (uint sliceIndex = 0u; sliceIndex < config.sliceCount; ++sliceIndex)
    {
        const float sliceExtinction = 1.0f - saturate(sliceTransmittance[sliceIndex]);
        extinctionTexture[uint3(lowPixel, sliceIndex)] = sliceExtinction;
        cumulativeTransmittance *= (1.0f - sliceExtinction);
        integratedTransmittanceTexture[uint3(lowPixel, sliceIndex)] = cumulativeTransmittance;
    }

    occupancyTexture[lowPixel] = 1.0f - cumulativeTransmittance;

    if (IsFixedSliceScalarVBOITDebugOutput(perFrameBuffer.outputType))
    {
        RWTexture2D<uint2> debugVisTex = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::DebugVisualization)];
        const uint2 payload = PackDebugFloat3(cumulativeTransmittance.xxx);
        [loop]
        for (uint fullY = tileMin.y; fullY < tileMax.y; ++fullY)
        {
            [loop]
            for (uint fullX = tileMin.x; fullX < tileMax.x; ++fullX)
            {
                WriteDebugPixel(debugVisTex, uint2(fullX, fullY), payload);
            }
        }
    }
}