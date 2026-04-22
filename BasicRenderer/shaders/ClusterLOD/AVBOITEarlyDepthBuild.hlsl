#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "PerPassRootConstants/clodAVBOITEarlyDepthBuildRootConstants.h"

float ComputeConservativeEarlyDepthWarpedSliceCoordinate(
    CLodAVBOITConfig config,
    uint zeroTransmittanceSlice)
{
    const float guaranteedZeroLookupSliceCoordinate = (float)zeroTransmittanceSlice + 1.0f;
    return guaranteedZeroLookupSliceCoordinate + max(config.lookupDepthBiasInSlices, 0.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodAVBOITEarlyDepthBuildCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<CLodAVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_EARLY_DEPTH_BUILD_CONFIG_DESCRIPTOR_INDEX];
    Texture2D<uint> zeroTransmittanceSliceTexture =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_EARLY_DEPTH_BUILD_ZERO_SLICE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodAVBOITEarlyDepthTileIndirectCommand> tileCommandsBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_EARLY_DEPTH_BUILD_COMMANDS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> tileCommandCountBuffer =
        ResourceDescriptorHeap[CLOD_AVBOIT_VBOIT_EARLY_DEPTH_BUILD_COMMAND_COUNT_DESCRIPTOR_INDEX];
    const CLodAVBOITConfig config = configBuffer[0];

    if (dispatchThreadId.x >= config.lowResolutionWidth ||
        dispatchThreadId.y >= config.lowResolutionHeight ||
        config.sliceCount == 0u)
    {
        return;
    }

    const uint2 lowPixel = dispatchThreadId.xy;
    const uint zeroTransmittanceSlice = zeroTransmittanceSliceTexture[lowPixel];
    if (zeroTransmittanceSlice >= config.sliceCount)
    {
        return;
    }

    const float conservativeWarpedSliceCoordinate =
        ComputeConservativeEarlyDepthWarpedSliceCoordinate(config, zeroTransmittanceSlice);
    if (conservativeWarpedSliceCoordinate > (float)(config.sliceCount - 1u))
    {
        return;
    }

    uint commandIndex = 0u;
    InterlockedAdd(tileCommandCountBuffer[0], 1u, commandIndex);

    CLodAVBOITEarlyDepthTileIndirectCommand command;
    command.lowResolutionPixelX = lowPixel.x;
    command.lowResolutionPixelY = lowPixel.y;
    command.zeroTransmittanceSlice = zeroTransmittanceSlice;
    command.vertexCountPerInstance = 4u;
    command.instanceCount = 1u;
    command.startVertexLocation = 0u;
    command.startInstanceLocation = 0u;
    tileCommandsBuffer[commandIndex] = command;
}