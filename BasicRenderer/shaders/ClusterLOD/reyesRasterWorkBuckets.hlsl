#include "include/cbuffers.hlsli"
#include "include/reyesPatchCommon.hlsli"
#include "include/structs.hlsli"
#include "include/waveIntrinsicsHelpers.hlsli"
#include "include/indirectCommands.hlsli"
#include "PerPassRootConstants/clodReyesRasterWorkBucketRootConstants.h"

struct RasterBucketsHistogramIndirectCommand
{
    uint clusterCount;
    uint xDim;
    uint dispatchX;
    uint dispatchY;
    uint dispatchZ;
};

static const uint CLOD_REYES_RASTER_BUCKET_GROUP_SIZE = 8u;
static const uint CLOD_REYES_RASTER_PACK_GROUP_SIZE = 64u;

RasterizeClustersCommand BuildRasterizeClustersCommand(uint baseOffset, uint bucketIndex, uint count)
{
    RasterizeClustersCommand cmd = (RasterizeClustersCommand)0;
    if (count == 0u)
    {
        return cmd;
    }

    const uint kMaxDim = 65535u;
    uint dispatchX = (uint)ceil(sqrt((float)count));
    dispatchX = min(dispatchX, kMaxDim);

    uint dispatchY = (count + dispatchX - 1u) / dispatchX;
    dispatchY = min(dispatchY, kMaxDim);

    cmd.baseClusterOffset = baseOffset;
    cmd.xDim = dispatchX;
    cmd.rasterBucketID = bucketIndex;
    cmd.dispatchX = dispatchX;
    cmd.dispatchY = dispatchY;
    cmd.dispatchZ = 1u;
    return cmd;
}

[shader("compute")]
[numthreads(CLOD_REYES_RASTER_BUCKET_GROUP_SIZE, 1, 1)]
void HistogramReyesRasterWorkBucketsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint xDimThreads = IndirectCommandSignatureRootConstant1 * CLOD_REYES_RASTER_BUCKET_GROUP_SIZE;
    const uint linearizedID = dispatchThreadId.x + dispatchThreadId.y * xDimThreads;

    StructuredBuffer<uint> rasterWorkCounterBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_WORK_COUNTER_DESCRIPTOR_INDEX];
    const uint rasterWorkCount = rasterWorkCounterBuffer.Load(0);
    if (linearizedID >= rasterWorkCount)
    {
        return;
    }

    StructuredBuffer<CLodReyesRasterWorkEntry> rasterWorkBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_WORK_BUFFER_DESCRIPTOR_INDEX];
    const uint rasterBucketIndex = rasterWorkBuffer[linearizedID].rasterBucketIndex;

    const uint4 mask = WaveMatch(rasterBucketIndex);
    if (IsWaveGroupLeader(mask))
    {
        const uint groupSize = CountBits128(mask);
        RWStructuredBuffer<uint> histogramBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_HISTOGRAM_DESCRIPTOR_INDEX];
        InterlockedAdd(histogramBuffer[rasterBucketIndex], groupSize);
    }
}

[shader("compute")]
[numthreads(CLOD_REYES_RASTER_BUCKET_GROUP_SIZE, 1, 1)]
void CompactReyesRasterWorkCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint xDimThreads = IndirectCommandSignatureRootConstant1 * CLOD_REYES_RASTER_BUCKET_GROUP_SIZE;
    const uint linearizedID = dispatchThreadId.x + dispatchThreadId.y * xDimThreads;

    StructuredBuffer<uint> rasterWorkCounterBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_WORK_COUNTER_DESCRIPTOR_INDEX];
    const uint rasterWorkCount = rasterWorkCounterBuffer.Load(0);

    StructuredBuffer<CLodReyesRasterWorkEntry> rasterWorkBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_WORK_BUFFER_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> offsetsBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_OFFSETS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> writeCursorBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_WRITE_CURSOR_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> compactedRasterWorkIndices = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_COMPACTED_WORK_INDICES_DESCRIPTOR_INDEX];

    if (linearizedID < rasterWorkCount)
    {
        const uint bucketIndex = rasterWorkBuffer[linearizedID].rasterBucketIndex;
        uint localOffset = 0u;
        InterlockedAdd(writeCursorBuffer[bucketIndex], 1u, localOffset);
        const uint dst = offsetsBuffer[bucketIndex] + localOffset;
        compactedRasterWorkIndices[dst] = linearizedID;
    }
}

[shader("compute")]
[numthreads(CLOD_REYES_RASTER_BUCKET_GROUP_SIZE, 1, 1)]
void EmitPackedReyesRasterWorkGroupsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint xDimThreads = IndirectCommandSignatureRootConstant1 * CLOD_REYES_RASTER_BUCKET_GROUP_SIZE;
    const uint compactedWorkIndex = dispatchThreadId.x + dispatchThreadId.y * xDimThreads;

    StructuredBuffer<uint> rasterWorkCounterBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_WORK_COUNTER_DESCRIPTOR_INDEX];
    const uint rasterWorkCount = rasterWorkCounterBuffer.Load(0);
    if (compactedWorkIndex >= rasterWorkCount)
    {
        return;
    }

    StructuredBuffer<CLodReyesRasterWorkEntry> rasterWorkBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_WORK_BUFFER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> histogramBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_HISTOGRAM_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> offsetsBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_OFFSETS_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> compactedRasterWorkIndices = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_COMPACTED_WORK_INDICES_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesPackedRasterWorkGroupEntry> packedRasterWorkGroups = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_PACKED_WORK_GROUPS_DESCRIPTOR_INDEX];

    const uint rasterWorkIndex = compactedRasterWorkIndices[compactedWorkIndex];
    const CLodReyesRasterWorkEntry rasterWorkEntry = rasterWorkBuffer[rasterWorkIndex];
    const uint bucketIndex = rasterWorkEntry.rasterBucketIndex;
    const uint bucketBaseOffset = offsetsBuffer[bucketIndex];
    const uint rawCount = histogramBuffer[bucketIndex];
    const uint localOffset = compactedWorkIndex - bucketBaseOffset;
    if (localOffset >= rawCount || (localOffset % CLodReyesHardwareRasterPackedEntryCount) != 0u)
    {
        return;
    }

    CLodReyesPackedRasterWorkGroupEntry packedGroup = (CLodReyesPackedRasterWorkGroupEntry)0;
    packedGroup.firstCompactedRasterWorkIndex = compactedWorkIndex;
    packedGroup.rasterWorkEntryCount = min(rawCount - localOffset, CLodReyesHardwareRasterPackedEntryCount);

    for (uint entryIndex = 0u; entryIndex < packedGroup.rasterWorkEntryCount; ++entryIndex)
    {
        const uint localCompactedWorkIndex = compactedWorkIndex + entryIndex;
        const uint localRasterWorkIndex = compactedRasterWorkIndices[localCompactedWorkIndex];
        packedGroup.requestedMicroTriangleCount += rasterWorkBuffer[localRasterWorkIndex].microTriangleCount;
    }

    const uint packedGroupOffset = localOffset / CLodReyesHardwareRasterPackedEntryCount;
    packedRasterWorkGroups[bucketBaseOffset + packedGroupOffset] = packedGroup;
}

[shader("compute")]
[numthreads(CLOD_REYES_RASTER_PACK_GROUP_SIZE, 1, 1)]
void PackReyesRasterWorkGroupsAndBuildIndirectArgsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint bucketIndex = dispatchThreadId.x;
    const uint numBuckets = CLOD_REYES_RASTER_BUCKET_NUM_BUCKETS;
    if (bucketIndex >= numBuckets)
    {
        return;
    }

    StructuredBuffer<uint> offsetsBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_OFFSETS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> histogramBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_HISTOGRAM_DESCRIPTOR_INDEX];
    RWStructuredBuffer<RasterizeClustersCommand> indirectArgsBuffer = ResourceDescriptorHeap[CLOD_REYES_RASTER_BUCKET_INDIRECT_ARGS_DESCRIPTOR_INDEX];

    const uint rawCount = histogramBuffer[bucketIndex];
    const uint baseOffset = offsetsBuffer[bucketIndex];

    const uint packedGroupCount = (rawCount + CLodReyesHardwareRasterPackedEntryCount - 1u) / CLodReyesHardwareRasterPackedEntryCount;

    histogramBuffer[bucketIndex] = packedGroupCount;
    indirectArgsBuffer[bucketIndex] = BuildRasterizeClustersCommand(baseOffset, bucketIndex, packedGroupCount);
}
