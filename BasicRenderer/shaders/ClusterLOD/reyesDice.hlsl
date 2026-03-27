#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/reyesPatchCommon.hlsli"
#include "PerPassRootConstants/clodReyesDiceRootConstants.h"

static const uint REYES_DICE_GROUP_SIZE = 64u;

float2 DecodeReyesBarycentricsUV(uint encoded)
{
    const float3 barycentrics = ReyesDecodePatchBarycentrics(encoded);
    return barycentrics.yz;
}

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

    const float2 baryUV0 = DecodeReyesBarycentricsUV(diceEntry.domainVertex0Encoded);
    const float2 baryUV1 = DecodeReyesBarycentricsUV(diceEntry.domainVertex1Encoded);
    const float2 baryUV2 = DecodeReyesBarycentricsUV(diceEntry.domainVertex2Encoded);
    const float signedArea2 =
        (baryUV1.x - baryUV0.x) * (baryUV2.y - baryUV0.y) -
        (baryUV1.y - baryUV0.y) * (baryUV2.x - baryUV0.x);
    const bool validPatch = abs(signedArea2) > (1.0f / REYES_BARYCENTRIC_COORD_SCALE);
    if (!validPatch)
    {
        return;
    }

    InterlockedAdd(telemetryBuffer[0].dicedPatchCount, 1u);
    InterlockedAdd(telemetryBuffer[0].dicedTriangleEstimateCount, estimatedTriangles);
    InterlockedAdd(telemetryBuffer[0].dicedVertexEstimateCount, estimatedVertices);
}