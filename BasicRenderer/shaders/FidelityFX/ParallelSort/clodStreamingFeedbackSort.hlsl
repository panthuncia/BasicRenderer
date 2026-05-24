// This file is derived from the FidelityFX SDK Parallel Sort shaders.
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "Include/cbuffers.hlsli"
#include "PerPassRootConstants/clodStreamingFeedbackSortRootConstants.h"

#define FFX_PARALLELSORT_SORT_BITS_PER_PASS 4u
#define FFX_PARALLELSORT_SORT_BIN_COUNT 16u
#define FFX_PARALLELSORT_ELEMENTS_PER_THREAD 4u
#define FFX_PARALLELSORT_THREADGROUP_SIZE 128u
#define FFX_PARALLELSORT_MAX_THREADGROUPS_TO_RUN 800u
#define FFX_PARALLELSORT_RADIX_ITERATIONS 8u

struct ParallelSortConstants
{
    uint numKeys;
    int numBlocksPerThreadGroup;
    uint numThreadGroups;
    uint numThreadGroupsWithAdditionalBlocks;
    uint numReduceThreadgroupPerBin;
    uint numScanValues;
    uint shift;
    uint padding;
};

struct CLodStreamingRequest
{
    uint groupGlobalIndex;
    uint meshInstanceIndex;
    uint meshBufferIndex;
    uint viewId;
};

CLodStreamingRequest EmptyStreamingRequest()
{
    CLodStreamingRequest request;
    request.groupGlobalIndex = 0u;
    request.meshInstanceIndex = 0u;
    request.meshBufferIndex = 0u;
    request.viewId = 0u;
    return request;
}

StructuredBuffer<uint> SortRequestCounter()
{
    return ResourceDescriptorHeap[CLOD_STREAMING_SORT_REQUEST_COUNTER_DESCRIPTOR_INDEX];
}

RWStructuredBuffer<ParallelSortConstants> SortConstantsBuffer()
{
    return ResourceDescriptorHeap[CLOD_STREAMING_SORT_CONSTANTS_DESCRIPTOR_INDEX];
}

RWStructuredBuffer<uint> CountScatterArgs()
{
    return ResourceDescriptorHeap[CLOD_STREAMING_SORT_COUNT_SCATTER_ARGS_DESCRIPTOR_INDEX];
}

RWStructuredBuffer<uint> ReduceScanArgs()
{
    return ResourceDescriptorHeap[CLOD_STREAMING_SORT_REDUCE_SCAN_ARGS_DESCRIPTOR_INDEX];
}

RWStructuredBuffer<uint> SourceKeys()
{
    return ResourceDescriptorHeap[CLOD_STREAMING_SORT_SOURCE_KEYS_DESCRIPTOR_INDEX];
}

RWStructuredBuffer<uint> DestKeys()
{
    return ResourceDescriptorHeap[CLOD_STREAMING_SORT_DEST_KEYS_DESCRIPTOR_INDEX];
}

RWStructuredBuffer<uint> SumTable()
{
    return ResourceDescriptorHeap[CLOD_STREAMING_SORT_SUM_TABLE_DESCRIPTOR_INDEX];
}

RWStructuredBuffer<uint> ReduceTable()
{
    return ResourceDescriptorHeap[CLOD_STREAMING_SORT_REDUCE_TABLE_DESCRIPTOR_INDEX];
}

RWStructuredBuffer<CLodStreamingRequest> SourcePayloads()
{
    return ResourceDescriptorHeap[CLOD_STREAMING_SORT_SOURCE_PAYLOADS_DESCRIPTOR_INDEX];
}

RWStructuredBuffer<CLodStreamingRequest> DestPayloads()
{
    return ResourceDescriptorHeap[CLOD_STREAMING_SORT_DEST_PAYLOADS_DESCRIPTOR_INDEX];
}

ParallelSortConstants SortConstants()
{
    RWStructuredBuffer<ParallelSortConstants> constantsBuffer = SortConstantsBuffer();
    return constantsBuffer[CLOD_STREAMING_SORT_ITERATION_INDEX];
}

uint DivideRoundingUp(uint value, uint divisor)
{
    return (value + divisor - 1u) / divisor;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void CLodStreamingFeedbackSortSetupCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    (void)dispatchThreadId;

    const uint blockSize = FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE;
    const uint numKeys = min(SortRequestCounter()[0], CLOD_STREAMING_SORT_REQUEST_CAPACITY);
    const uint numBlocks = DivideRoundingUp(numKeys, blockSize);

    uint numThreadGroupsToRun = FFX_PARALLELSORT_MAX_THREADGROUPS_TO_RUN;
    uint blocksPerThreadGroup = numBlocks / numThreadGroupsToRun;
    uint threadGroupsWithAdditionalBlocks = numBlocks % numThreadGroupsToRun;

    if (numBlocks < numThreadGroupsToRun)
    {
        blocksPerThreadGroup = numBlocks == 0u ? 0u : 1u;
        numThreadGroupsToRun = max(numBlocks, 1u);
        threadGroupsWithAdditionalBlocks = 0u;
    }

    const uint numReducedThreadGroupsToRun =
        FFX_PARALLELSORT_SORT_BIN_COUNT *
        ((blockSize > numThreadGroupsToRun) ? 1u : DivideRoundingUp(numThreadGroupsToRun, blockSize));
    const uint numReduceThreadgroupPerBin = numReducedThreadGroupsToRun / FFX_PARALLELSORT_SORT_BIN_COUNT;

    RWStructuredBuffer<ParallelSortConstants> constantsBuffer = SortConstantsBuffer();
    for (uint iteration = 0u; iteration < FFX_PARALLELSORT_RADIX_ITERATIONS; ++iteration)
    {
        ParallelSortConstants constants;
        constants.numKeys = numKeys;
        constants.numBlocksPerThreadGroup = int(blocksPerThreadGroup);
        constants.numThreadGroups = numThreadGroupsToRun;
        constants.numThreadGroupsWithAdditionalBlocks = threadGroupsWithAdditionalBlocks;
        constants.numReduceThreadgroupPerBin = numReduceThreadgroupPerBin;
        constants.numScanValues = numReducedThreadGroupsToRun;
        constants.shift = iteration * FFX_PARALLELSORT_SORT_BITS_PER_PASS;
        constants.padding = 0u;
        constantsBuffer[iteration] = constants;
    }

    RWStructuredBuffer<uint> countScatterArgs = CountScatterArgs();
    countScatterArgs[0] = numThreadGroupsToRun;
    countScatterArgs[1] = 1u;
    countScatterArgs[2] = 1u;

    RWStructuredBuffer<uint> reduceScanArgs = ReduceScanArgs();
    reduceScanArgs[0] = numReducedThreadGroupsToRun;
    reduceScanArgs[1] = 1u;
    reduceScanArgs[2] = 1u;
}

groupshared uint gHistogram[FFX_PARALLELSORT_THREADGROUP_SIZE * FFX_PARALLELSORT_SORT_BIN_COUNT];
groupshared uint gLdsSums[FFX_PARALLELSORT_THREADGROUP_SIZE];
groupshared uint gLds[FFX_PARALLELSORT_ELEMENTS_PER_THREAD][FFX_PARALLELSORT_THREADGROUP_SIZE];
groupshared uint gBinOffsetCache[FFX_PARALLELSORT_THREADGROUP_SIZE];
groupshared uint gLocalHistogram[FFX_PARALLELSORT_SORT_BIN_COUNT];
groupshared uint gLdsScratch[FFX_PARALLELSORT_THREADGROUP_SIZE];
groupshared CLodStreamingRequest gPayloadScratch[FFX_PARALLELSORT_THREADGROUP_SIZE];

uint ThreadgroupReduce(uint localSum, uint localId)
{
    uint waveReduced = WaveActiveSum(localSum);
    const uint waveId = localId / WaveGetLaneCount();
    if (WaveIsFirstLane())
    {
        gLdsSums[waveId] = waveReduced;
    }

    GroupMemoryBarrierWithGroupSync();

    if (waveId == 0u)
    {
        waveReduced = WaveActiveSum((localId < FFX_PARALLELSORT_THREADGROUP_SIZE / WaveGetLaneCount()) ? gLdsSums[localId] : 0u);
    }

    return waveReduced;
}

uint BlockScanPrefix(uint localSum, uint localId)
{
    uint wavePrefixed = WavePrefixSum(localSum);
    const uint waveId = localId / WaveGetLaneCount();
    const uint laneId = WaveGetLaneIndex();

    if (laneId == WaveGetLaneCount() - 1u)
    {
        gLdsSums[waveId] = wavePrefixed + localSum;
    }

    GroupMemoryBarrierWithGroupSync();

    if (waveId == 0u)
    {
        gLdsSums[localId] = WavePrefixSum(
            (localId < FFX_PARALLELSORT_THREADGROUP_SIZE / WaveGetLaneCount()) ? gLdsSums[localId] : 0u);
    }

    GroupMemoryBarrierWithGroupSync();

    return wavePrefixed + gLdsSums[waveId];
}

void CountUInt(uint localId, uint groupId)
{
    const ParallelSortConstants constants = SortConstants();
    for (int i = 0; i < FFX_PARALLELSORT_SORT_BIN_COUNT; ++i)
    {
        gHistogram[(i * FFX_PARALLELSORT_THREADGROUP_SIZE) + localId] = 0u;
    }

    GroupMemoryBarrierWithGroupSync();

    const uint blockSize = FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE;
    const uint numBlocksPerThreadGroup = uint(constants.numBlocksPerThreadGroup);
    const uint numThreadGroups = constants.numThreadGroups;
    const uint additionalBlocks = constants.numThreadGroupsWithAdditionalBlocks;
    const uint numKeys = constants.numKeys;

    uint threadgroupBlockStart = blockSize * numBlocksPerThreadGroup * groupId;
    uint numBlocksToProcess = numBlocksPerThreadGroup;

    if (groupId >= numThreadGroups - additionalBlocks)
    {
        threadgroupBlockStart += (groupId - (numThreadGroups - additionalBlocks)) * blockSize;
        numBlocksToProcess++;
    }

    uint blockIndex = threadgroupBlockStart + localId;
    RWStructuredBuffer<uint> sourceKeys = SourceKeys();

    for (uint blockCount = 0u; blockCount < numBlocksToProcess; ++blockCount, blockIndex += blockSize)
    {
        uint dataIndex = blockIndex;
        uint srcKeys[FFX_PARALLELSORT_ELEMENTS_PER_THREAD];
        srcKeys[0] = (dataIndex < numKeys) ? sourceKeys[dataIndex] : 0xffffffffu;
        srcKeys[1] = (dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE < numKeys) ? sourceKeys[dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE] : 0xffffffffu;
        srcKeys[2] = (dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 2u < numKeys) ? sourceKeys[dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 2u] : 0xffffffffu;
        srcKeys[3] = (dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 3u < numKeys) ? sourceKeys[dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 3u] : 0xffffffffu;

        for (uint i = 0u; i < FFX_PARALLELSORT_ELEMENTS_PER_THREAD; ++i)
        {
            if (dataIndex < numKeys)
            {
                const uint localKey = (srcKeys[i] >> constants.shift) & 0xfu;
                InterlockedAdd(gHistogram[(localKey * FFX_PARALLELSORT_THREADGROUP_SIZE) + localId], 1u);
                dataIndex += FFX_PARALLELSORT_THREADGROUP_SIZE;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (localId < FFX_PARALLELSORT_SORT_BIN_COUNT)
    {
        uint sum = 0u;
        for (int i = 0; i < FFX_PARALLELSORT_THREADGROUP_SIZE; ++i)
        {
            sum += gHistogram[localId * FFX_PARALLELSORT_THREADGROUP_SIZE + i];
        }
        SumTable()[localId * numThreadGroups + groupId] = sum;
    }
}

void ReduceCount(uint localId, uint groupId)
{
    const ParallelSortConstants constants = SortConstants();
    const uint numReduceThreadgroupPerBin = constants.numReduceThreadgroupPerBin;
    const uint numThreadGroups = constants.numThreadGroups;
    const uint binId = groupId / numReduceThreadgroupPerBin;
    const uint binOffset = binId * numThreadGroups;
    const uint baseIndex = (groupId % numReduceThreadgroupPerBin) * FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE;

    RWStructuredBuffer<uint> sumTable = SumTable();
    uint threadgroupSum = 0u;
    for (uint i = 0u; i < FFX_PARALLELSORT_ELEMENTS_PER_THREAD; ++i)
    {
        const uint dataIndex = baseIndex + (i * FFX_PARALLELSORT_THREADGROUP_SIZE) + localId;
        threadgroupSum += (dataIndex < numThreadGroups) ? sumTable[binOffset + dataIndex] : 0u;
    }

    threadgroupSum = ThreadgroupReduce(threadgroupSum, localId);
    if (localId == 0u)
    {
        ReduceTable()[groupId] = threadgroupSum;
    }
}

void ScanPrefix(uint numValuesToScan, uint localId, uint groupId, uint binOffset, uint baseIndex, bool addPartialSums)
{
    RWStructuredBuffer<uint> sumTable = SumTable();
    RWStructuredBuffer<uint> reduceTable = ReduceTable();

    for (uint i = 0u; i < FFX_PARALLELSORT_ELEMENTS_PER_THREAD; ++i)
    {
        const uint dataIndex = baseIndex + (i * FFX_PARALLELSORT_THREADGROUP_SIZE) + localId;
        const uint col = ((i * FFX_PARALLELSORT_THREADGROUP_SIZE) + localId) / FFX_PARALLELSORT_ELEMENTS_PER_THREAD;
        const uint row = ((i * FFX_PARALLELSORT_THREADGROUP_SIZE) + localId) % FFX_PARALLELSORT_ELEMENTS_PER_THREAD;
        const uint value = addPartialSums
            ? ((dataIndex < numValuesToScan) ? sumTable[binOffset + dataIndex] : 0u)
            : ((dataIndex < numValuesToScan) ? reduceTable[binOffset + dataIndex] : 0u);
        gLds[row][col] = value;
    }

    GroupMemoryBarrierWithGroupSync();

    uint threadgroupSum = 0u;
    for (uint i = 0u; i < FFX_PARALLELSORT_ELEMENTS_PER_THREAD; ++i)
    {
        const uint tmp = gLds[i][localId];
        gLds[i][localId] = threadgroupSum;
        threadgroupSum += tmp;
    }

    threadgroupSum = BlockScanPrefix(threadgroupSum, localId);

    const uint partialSum = addPartialSums ? reduceTable[groupId] : 0u;

    for (uint i = 0u; i < FFX_PARALLELSORT_ELEMENTS_PER_THREAD; ++i)
    {
        gLds[i][localId] += threadgroupSum;
    }

    GroupMemoryBarrierWithGroupSync();

    for (uint i = 0u; i < FFX_PARALLELSORT_ELEMENTS_PER_THREAD; ++i)
    {
        const uint dataIndex = baseIndex + (i * FFX_PARALLELSORT_THREADGROUP_SIZE) + localId;
        const uint col = ((i * FFX_PARALLELSORT_THREADGROUP_SIZE) + localId) / FFX_PARALLELSORT_ELEMENTS_PER_THREAD;
        const uint row = ((i * FFX_PARALLELSORT_THREADGROUP_SIZE) + localId) % FFX_PARALLELSORT_ELEMENTS_PER_THREAD;
        if (dataIndex < numValuesToScan)
        {
            if (addPartialSums)
            {
                sumTable[binOffset + dataIndex] = gLds[row][col] + partialSum;
            }
            else
            {
                reduceTable[binOffset + dataIndex] = gLds[row][col] + partialSum;
            }
        }
    }
}

void ScatterUInt(uint localId, uint groupId)
{
    const ParallelSortConstants constants = SortConstants();
    const uint numBlocksPerThreadGroup = uint(constants.numBlocksPerThreadGroup);
    const uint numThreadGroups = constants.numThreadGroups;
    const uint additionalBlocks = constants.numThreadGroupsWithAdditionalBlocks;
    const uint numKeys = constants.numKeys;

    RWStructuredBuffer<uint> sourceKeys = SourceKeys();
    RWStructuredBuffer<uint> destKeys = DestKeys();
    RWStructuredBuffer<uint> sumTable = SumTable();
    RWStructuredBuffer<CLodStreamingRequest> sourcePayloads = SourcePayloads();
    RWStructuredBuffer<CLodStreamingRequest> destPayloads = DestPayloads();

    if (localId < FFX_PARALLELSORT_SORT_BIN_COUNT)
    {
        gBinOffsetCache[localId] = sumTable[localId * numThreadGroups + groupId];
    }

    GroupMemoryBarrierWithGroupSync();

    const uint blockSize = FFX_PARALLELSORT_ELEMENTS_PER_THREAD * FFX_PARALLELSORT_THREADGROUP_SIZE;
    uint threadgroupBlockStart = blockSize * numBlocksPerThreadGroup * groupId;
    uint numBlocksToProcess = numBlocksPerThreadGroup;

    if (groupId >= numThreadGroups - additionalBlocks)
    {
        threadgroupBlockStart += (groupId - (numThreadGroups - additionalBlocks)) * blockSize;
        numBlocksToProcess++;
    }

    uint blockIndex = threadgroupBlockStart + localId;
    for (uint blockCount = 0u; blockCount < numBlocksToProcess; ++blockCount, blockIndex += blockSize)
    {
        uint dataIndex = blockIndex;
        uint srcKeys[FFX_PARALLELSORT_ELEMENTS_PER_THREAD];
        srcKeys[0] = (dataIndex < numKeys) ? sourceKeys[dataIndex] : 0xffffffffu;
        srcKeys[1] = (dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE < numKeys) ? sourceKeys[dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE] : 0xffffffffu;
        srcKeys[2] = (dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 2u < numKeys) ? sourceKeys[dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 2u] : 0xffffffffu;
        srcKeys[3] = (dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 3u < numKeys) ? sourceKeys[dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 3u] : 0xffffffffu;

        CLodStreamingRequest srcPayloads[FFX_PARALLELSORT_ELEMENTS_PER_THREAD];
        srcPayloads[0] = EmptyStreamingRequest();
        srcPayloads[1] = EmptyStreamingRequest();
        srcPayloads[2] = EmptyStreamingRequest();
        srcPayloads[3] = EmptyStreamingRequest();
        if (dataIndex < numKeys) srcPayloads[0] = sourcePayloads[dataIndex];
        if (dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE < numKeys) srcPayloads[1] = sourcePayloads[dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE];
        if (dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 2u < numKeys) srcPayloads[2] = sourcePayloads[dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 2u];
        if (dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 3u < numKeys) srcPayloads[3] = sourcePayloads[dataIndex + FFX_PARALLELSORT_THREADGROUP_SIZE * 3u];

        for (uint i = 0u; i < FFX_PARALLELSORT_ELEMENTS_PER_THREAD; ++i)
        {
            if (localId < FFX_PARALLELSORT_SORT_BIN_COUNT)
            {
                gLocalHistogram[localId] = 0u;
            }

            uint localKey = srcKeys[i];
            CLodStreamingRequest localPayload = srcPayloads[i];

            for (uint bitShift = 0u; bitShift < FFX_PARALLELSORT_SORT_BITS_PER_PASS; bitShift += 2u)
            {
                const uint keyIndex = (localKey >> constants.shift) & 0xfu;
                const uint bitKey = (keyIndex >> bitShift) & 0x3u;
                uint packedHistogram = 1u << (bitKey * 8u);
                uint localSum = BlockScanPrefix(packedHistogram, localId);

                if (localId == FFX_PARALLELSORT_THREADGROUP_SIZE - 1u)
                {
                    gLdsScratch[0] = localSum + packedHistogram;
                }

                GroupMemoryBarrierWithGroupSync();

                packedHistogram = gLdsScratch[0];
                packedHistogram = (packedHistogram << 8u) + (packedHistogram << 16u) + (packedHistogram << 24u);
                localSum += packedHistogram;

                const uint keyOffset = (localSum >> (bitKey * 8u)) & 0xffu;

                gLdsSums[keyOffset] = localKey;
                gPayloadScratch[keyOffset] = localPayload;
                GroupMemoryBarrierWithGroupSync();

                localKey = gLdsSums[localId];
                localPayload = gPayloadScratch[localId];
                GroupMemoryBarrierWithGroupSync();
            }

            const uint keyIndex = (localKey >> constants.shift) & 0xfu;
            InterlockedAdd(gLocalHistogram[keyIndex], 1u);

            GroupMemoryBarrierWithGroupSync();

            const uint histogramPrefixSum = WavePrefixSum(localId < FFX_PARALLELSORT_SORT_BIN_COUNT ? gLocalHistogram[localId] : 0u);
            if (localId < FFX_PARALLELSORT_SORT_BIN_COUNT)
            {
                gLdsScratch[localId] = histogramPrefixSum;
            }

            const uint globalOffset = gBinOffsetCache[keyIndex];
            GroupMemoryBarrierWithGroupSync();

            const uint localOffset = localId - gLdsScratch[keyIndex];
            const uint totalOffset = globalOffset + localOffset;

            if (totalOffset < numKeys)
            {
                destKeys[totalOffset] = localKey;
                destPayloads[totalOffset] = localPayload;
            }

            GroupMemoryBarrierWithGroupSync();

            if (localId < FFX_PARALLELSORT_SORT_BIN_COUNT)
            {
                gBinOffsetCache[localId] += gLocalHistogram[localId];
            }

            dataIndex += FFX_PARALLELSORT_THREADGROUP_SIZE;
        }
    }
}

[shader("compute")]
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void CLodStreamingFeedbackSortCountCS(uint localId : SV_GroupThreadID, uint groupId : SV_GroupID)
{
    CountUInt(localId, groupId);
}

[shader("compute")]
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void CLodStreamingFeedbackSortReduceCS(uint localId : SV_GroupThreadID, uint groupId : SV_GroupID)
{
    ReduceCount(localId, groupId);
}

[shader("compute")]
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void CLodStreamingFeedbackSortScanCS(uint localId : SV_GroupThreadID, uint groupId : SV_GroupID)
{
    const ParallelSortConstants constants = SortConstants();
    ScanPrefix(constants.numScanValues, localId, groupId, 0u, 0u, false);
}

[shader("compute")]
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void CLodStreamingFeedbackSortScanAddCS(uint localId : SV_GroupThreadID, uint groupId : SV_GroupID)
{
    const ParallelSortConstants constants = SortConstants();
    const uint binOffset = (groupId / constants.numReduceThreadgroupPerBin) * constants.numThreadGroups;
    const uint baseIndex = (groupId % constants.numReduceThreadgroupPerBin) *
        FFX_PARALLELSORT_ELEMENTS_PER_THREAD *
        FFX_PARALLELSORT_THREADGROUP_SIZE;
    ScanPrefix(constants.numThreadGroups, localId, groupId, binOffset, baseIndex, true);
}

[shader("compute")]
[numthreads(FFX_PARALLELSORT_THREADGROUP_SIZE, 1, 1)]
void CLodStreamingFeedbackSortScatterCS(uint localId : SV_GroupThreadID, uint groupId : SV_GroupID)
{
    ScatterUInt(localId, groupId);
}
