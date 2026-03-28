#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/reyesPatchCommon.hlsli"
#include "PerPassRootConstants/clodReyesDiceRootConstants.h"

static const uint REYES_DICE_GROUP_SIZE = 64u;

[shader("compute")]
[numthreads(REYES_DICE_GROUP_SIZE, 1, 1)]
void ReyesDiceCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<uint> diceQueueCounter = ResourceDescriptorHeap[CLOD_REYES_DICE_QUEUE_COUNTER_DESCRIPTOR_INDEX];
    const uint diceCount = diceQueueCounter[0];
    const uint diceIndex = dispatchThreadId.x;
    if (diceIndex >= diceCount)
    {
        return;
    }

    StructuredBuffer<CLodReyesDiceQueueEntry> diceQueue = ResourceDescriptorHeap[CLOD_REYES_DICE_QUEUE_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodReyesTessTableConfigEntry> tessTableConfigs = ResourceDescriptorHeap[CLOD_REYES_DICE_TESS_TABLE_CONFIGS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesTelemetry> telemetryBuffer = ResourceDescriptorHeap[CLOD_REYES_DICE_TELEMETRY_DESCRIPTOR_INDEX];

    const CLodReyesDiceQueueEntry diceEntry = diceQueue[diceIndex];
    const uint tessSegments = ReyesGetDicePatchSegments(diceEntry);
    const uint estimatedTriangles = ReyesGetDicePatchMicroTriangleCount(tessTableConfigs, diceEntry);
    const uint estimatedVertices = ReyesGetDicePatchVertexCount(tessTableConfigs, diceEntry);

    const bool validPatch = ReyesPatchDomainHasValidSimplex(
        diceEntry.domainVertex0UV,
        diceEntry.domainVertex1UV,
        diceEntry.domainVertex2UV);
    if (!validPatch)
    {
        InterlockedAdd(telemetryBuffer[0].invalidDicePatchDomainCount, 1u);
        return;
    }

    InterlockedAdd(telemetryBuffer[0].dicedPatchCount, 1u);
    InterlockedAdd(telemetryBuffer[0].dicedTriangleEstimateCount, estimatedTriangles);
    InterlockedAdd(telemetryBuffer[0].dicedVertexEstimateCount, estimatedVertices);
}