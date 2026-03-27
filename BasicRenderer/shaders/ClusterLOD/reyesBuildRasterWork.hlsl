#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/reyesPatchCommon.hlsli"
#include "PerPassRootConstants/clodReyesBuildRasterWorkRootConstants.h"

static const uint REYES_BUILD_RASTER_WORK_GROUP_SIZE = 64u;

[shader("compute")]
[numthreads(REYES_BUILD_RASTER_WORK_GROUP_SIZE, 1, 1)]
void BuildReyesRasterWorkCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<uint> diceQueueCounter = ResourceDescriptorHeap[CLOD_REYES_BUILD_RASTER_WORK_DICE_QUEUE_COUNTER_DESCRIPTOR_INDEX];
    const uint diceCount = diceQueueCounter[0];
    const uint diceIndex = dispatchThreadId.x;
    if (diceIndex >= diceCount)
    {
        return;
    }

    StructuredBuffer<CLodReyesDiceQueueEntry> diceQueue = ResourceDescriptorHeap[CLOD_REYES_BUILD_RASTER_WORK_DICE_QUEUE_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodReyesTessTableConfigEntry> tessTableConfigs = ResourceDescriptorHeap[CLOD_REYES_BUILD_RASTER_WORK_TESS_TABLE_CONFIGS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesRasterWorkEntry> rasterWorkBuffer = ResourceDescriptorHeap[CLOD_REYES_BUILD_RASTER_WORK_OUTPUT_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> rasterWorkCounter = ResourceDescriptorHeap[CLOD_REYES_BUILD_RASTER_WORK_OUTPUT_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesTelemetry> telemetryBuffer = ResourceDescriptorHeap[CLOD_REYES_BUILD_RASTER_WORK_TELEMETRY_DESCRIPTOR_INDEX];

    const CLodReyesDiceQueueEntry diceEntry = diceQueue[diceIndex];
    const uint microTriangleCount = ReyesGetDicePatchMicroTriangleCount(tessTableConfigs, diceEntry);
    if (microTriangleCount == 0u)
    {
        return;
    }

    InterlockedAdd(telemetryBuffer[0].patchRasterizedPatchCount, 1u);
    InterlockedAdd(telemetryBuffer[0].patchRasterizedMicroTriangleCount, microTriangleCount);

    const uint rasterBatchCount = (microTriangleCount + CLodReyesRasterBatchMicroTriangleCount - 1u) / CLodReyesRasterBatchMicroTriangleCount;
    uint firstRasterWorkIndex = 0u;
    InterlockedAdd(rasterWorkCounter[0], rasterBatchCount, firstRasterWorkIndex);

    if (firstRasterWorkIndex >= CLOD_REYES_BUILD_RASTER_WORK_CAPACITY)
    {
        return;
    }

    const uint availableRasterWorkCount = min(rasterBatchCount, CLOD_REYES_BUILD_RASTER_WORK_CAPACITY - firstRasterWorkIndex);
    [loop]
    for (uint batchIndex = 0u; batchIndex < availableRasterWorkCount; ++batchIndex)
    {
        CLodReyesRasterWorkEntry workEntry;
        workEntry.diceQueueIndex = diceIndex;
        workEntry.microTriangleOffset = batchIndex * CLodReyesRasterBatchMicroTriangleCount;
        workEntry.microTriangleCount = min(CLodReyesRasterBatchMicroTriangleCount, microTriangleCount - workEntry.microTriangleOffset);
        workEntry.reserved = 0u;
        rasterWorkBuffer[firstRasterWorkIndex + batchIndex] = workEntry;
    }
}