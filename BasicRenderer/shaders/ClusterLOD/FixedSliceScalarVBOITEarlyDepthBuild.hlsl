#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "PerPassRootConstants/clodFixedSliceScalarVBOITEarlyDepthBuildRootConstants.h"

float ComputeConservativeEarlyDepthWarpedSliceCoordinate(
    CLodFixedSliceScalarVBOITConfig config,
    uint zeroTransmittanceSlice)
{
    const float guaranteedZeroLookupSliceCoordinate = (float)zeroTransmittanceSlice + 1.0f;
    return guaranteedZeroLookupSliceCoordinate + max(config.lookupDepthBiasInSlices, 0.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodFixedSliceScalarVBOITEarlyDepthBuildCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<CLodFixedSliceScalarVBOITConfig> configBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_EARLY_DEPTH_BUILD_CONFIG_DESCRIPTOR_INDEX];
    Texture2D<uint> zeroTransmittanceSliceTexture =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_EARLY_DEPTH_BUILD_ZERO_SLICE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodFixedSliceScalarVBOITEarlyDepthTileIndirectCommand> tileCommandsBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_EARLY_DEPTH_BUILD_COMMANDS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> tileCommandCountBuffer =
        ResourceDescriptorHeap[CLOD_FIXED_SLICE_SCALAR_VBOIT_EARLY_DEPTH_BUILD_COMMAND_COUNT_DESCRIPTOR_INDEX];
    const CLodFixedSliceScalarVBOITConfig config = configBuffer[0];

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

    CLodFixedSliceScalarVBOITEarlyDepthTileIndirectCommand command;
    command.lowResolutionPixelX = lowPixel.x;
    command.lowResolutionPixelY = lowPixel.y;
    command.zeroTransmittanceSlice = zeroTransmittanceSlice;
    command.vertexCountPerInstance = 4u;
    command.instanceCount = 1u;
    command.startVertexLocation = 0u;
    command.startInstanceLocation = 0u;
    tileCommandsBuffer[commandIndex] = command;
}