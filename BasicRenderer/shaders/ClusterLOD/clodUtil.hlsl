#include "include/cbuffers.hlsli"
#include "include/clodVirtualShadowClipmap.hlsli"
#include "include/structs.hlsli"
#include "include/waveIntrinsicsHelpers.hlsli"
#include "PerPassRootConstants/clodCreateCommandRootConstants.h"
#include "PerPassRootConstants/clodHistogramRootConstants.h"
#include "PerPassRootConstants/clodPrefixScanRootConstants.h"
#include "PerPassRootConstants/clodPrefixOffsetsRootConstants.h"
#include "PerPassRootConstants/clodCompactionRootConstants.h"
#include "PerPassRootConstants/clodReyesCreateDispatchArgsRootConstants.h"
#include "PerPassRootConstants/clodReyesResetRootConstants.h"
#include "PerPassRootConstants/clodVirtualShadowAllocateRootConstants.h"
#include "PerPassRootConstants/clodVirtualShadowBuildPageListsRootConstants.h"
#include "PerPassRootConstants/clodVirtualShadowClearDirtyBitsRootConstants.h"
#include "PerPassRootConstants/clodVirtualShadowClearRootConstants.h"
#include "PerPassRootConstants/clodVirtualShadowGatherStatsRootConstants.h"
#include "PerPassRootConstants/clodVirtualShadowDirtyHierarchyRootConstants.h"
#include "PerPassRootConstants/clodVirtualShadowMarkRootConstants.h"
#include "PerPassRootConstants/clodVirtualShadowSetupRootConstants.h"
#include "PerPassRootConstants/clodReyesSplitRootConstants.h"
#include "include/indirectCommands.hlsli"
#include "include/clodStructs.hlsli"
#include "include/visibleClusterPacking.hlsli"

struct RasterBucketsHistogramIndirectCommand
{
    uint clusterCount;
    uint xDim;
    uint dispatchX, dispatchY, dispatchZ;
};

static const uint kCLodVirtualShadowDefaultClipmapCount = 6u;
groupshared uint gCLodVirtualShadowShouldClearPage;

bool CLodVirtualShadowShouldClearWrappedPage(int2 wrappedPageCoords, int clearOffsetX, int clearOffsetY, uint pageTableResolution)
{
    const int resolution = (int)pageTableResolution;
    if (resolution <= 0)
    {
        return false;
    }

    return
        ((clearOffsetX > 0) && (wrappedPageCoords.x < clearOffsetX)) ||
        ((clearOffsetX < 0) && (wrappedPageCoords.x > resolution + (clearOffsetX - 1))) ||
        ((clearOffsetY > 0) && (wrappedPageCoords.y < clearOffsetY)) ||
        ((clearOffsetY < 0) && (wrappedPageCoords.y > resolution + (clearOffsetY - 1)));
}

uint CLodVirtualShadowUnwrapPageCoord(uint wrappedCoord, uint offset, uint pageTableResolution)
{
    const uint wrappedOffset = pageTableResolution > 0u ? (offset % pageTableResolution) : 0u;
    return pageTableResolution > 0u
        ? ((wrappedCoord + wrappedOffset) % pageTableResolution)
        : 0u;
}

uint2 CLodVirtualShadowUnwrappedPageCoords(
    uint2 wrappedPageCoords,
    CLodVirtualShadowClipmapInfo clipmapInfo)
{
    return uint2(
        CLodVirtualShadowUnwrapPageCoord(wrappedPageCoords.x, clipmapInfo.pageOffsetX, kCLodVirtualShadowPageTableResolution),
        CLodVirtualShadowUnwrapPageCoord(wrappedPageCoords.y, clipmapInfo.pageOffsetY, kCLodVirtualShadowPageTableResolution));
}

void CLodVirtualShadowStatsIncrementSelected(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].selectedPixels[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].selectedPixels[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].selectedPixels[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].selectedPixels[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].selectedPixels[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].selectedPixels[5], 1u); break;
    }
}

void CLodVirtualShadowStatsIncrementProjectionRejected(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].projectionRejectedPixels[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].projectionRejectedPixels[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].projectionRejectedPixels[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].projectionRejectedPixels[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].projectionRejectedPixels[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].projectionRejectedPixels[5], 1u); break;
    }
}

void CLodVirtualShadowStatsIncrementRequestedPages(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].requestedPages[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].requestedPages[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].requestedPages[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].requestedPages[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].requestedPages[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].requestedPages[5], 1u); break;
    }
}

void CLodVirtualShadowStatsIncrementNonZero(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].nonZeroPageTableEntries[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].nonZeroPageTableEntries[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].nonZeroPageTableEntries[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].nonZeroPageTableEntries[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].nonZeroPageTableEntries[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].nonZeroPageTableEntries[5], 1u); break;
    }
}

void CLodVirtualShadowStatsIncrementPreAllocateNonZero(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].preAllocateNonZeroPageTableEntries[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].preAllocateNonZeroPageTableEntries[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].preAllocateNonZeroPageTableEntries[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].preAllocateNonZeroPageTableEntries[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].preAllocateNonZeroPageTableEntries[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].preAllocateNonZeroPageTableEntries[5], 1u); break;
    }
}

void CLodVirtualShadowStatsIncrementAllocated(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].allocatedPageTableEntries[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].allocatedPageTableEntries[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].allocatedPageTableEntries[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].allocatedPageTableEntries[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].allocatedPageTableEntries[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].allocatedPageTableEntries[5], 1u); break;
    }
}

void CLodVirtualShadowStatsIncrementDirty(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].dirtyPageTableEntries[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].dirtyPageTableEntries[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].dirtyPageTableEntries[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].dirtyPageTableEntries[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].dirtyPageTableEntries[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].dirtyPageTableEntries[5], 1u); break;
    }
}

void CLodVirtualShadowStatsIncrementPreAllocateDirty(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].preAllocateDirtyPageTableEntries[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].preAllocateDirtyPageTableEntries[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].preAllocateDirtyPageTableEntries[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].preAllocateDirtyPageTableEntries[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].preAllocateDirtyPageTableEntries[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].preAllocateDirtyPageTableEntries[5], 1u); break;
    }
}

void CLodVirtualShadowStatsIncrementSetupWrappedClear(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].setupWrappedClearedPageTableEntries[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].setupWrappedClearedPageTableEntries[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].setupWrappedClearedPageTableEntries[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].setupWrappedClearedPageTableEntries[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].setupWrappedClearedPageTableEntries[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].setupWrappedClearedPageTableEntries[5], 1u); break;
    }
}

void CLodVirtualShadowStatsIncrementSetupStaleDirtyClear(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].setupStaleDirtyClearedPageTableEntries[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].setupStaleDirtyClearedPageTableEntries[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].setupStaleDirtyClearedPageTableEntries[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].setupStaleDirtyClearedPageTableEntries[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].setupStaleDirtyClearedPageTableEntries[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].setupStaleDirtyClearedPageTableEntries[5], 1u); break;
    }
}

void CLodVirtualShadowStatsIncrementMarkResidentCleanHit(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].markResidentCleanHits[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].markResidentCleanHits[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].markResidentCleanHits[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].markResidentCleanHits[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].markResidentCleanHits[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].markResidentCleanHits[5], 1u); break;
    }
}

void CLodVirtualShadowStatsIncrementMarkResidentDirtyHit(RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer, uint clipmapIndex)
{
    switch (clipmapIndex)
    {
    case 0u: InterlockedAdd(statsBuffer[0].markResidentDirtyHits[0], 1u); break;
    case 1u: InterlockedAdd(statsBuffer[0].markResidentDirtyHits[1], 1u); break;
    case 2u: InterlockedAdd(statsBuffer[0].markResidentDirtyHits[2], 1u); break;
    case 3u: InterlockedAdd(statsBuffer[0].markResidentDirtyHits[3], 1u); break;
    case 4u: InterlockedAdd(statsBuffer[0].markResidentDirtyHits[4], 1u); break;
    case 5u: InterlockedAdd(statsBuffer[0].markResidentDirtyHits[5], 1u); break;
    }
}

uint CLodCalculateShadowCascadeIndex(float depth, uint numCascadeSplits, float4 cascadeSplits)
{
    [unroll]
    for (uint i = 0u; i < 4u; ++i)
    {
        if (i >= numCascadeSplits)
        {
            break;
        }

        if (depth < cascadeSplits[i])
        {
            return i;
        }
    }

    return numCascadeSplits > 0u ? (numCascadeSplits - 1u) : 0u;
}

float3 CLodWorldSpaceFromScreenUv(float2 screenUv, float linearDepth, Camera camera)
{
    const float2 ndc = float2(screenUv.x * 2.0f - 1.0f, (1.0f - screenUv.y) * 2.0f - 1.0f);
    const float4 clipPos = float4(ndc, 1.0f, 1.0f);
    const float4 viewPosH = mul(clipPos, camera.projectionInverse);
    const float3 positionVS = viewPosH.xyz * linearDepth;
    return mul(float4(positionVS, 1.0f), camera.viewInverse).xyz;
}

float3 CLodRayPlaneIntersection(float3 rayDirection, float3 rayOrigin, float3 planeNormal, float3 planeOrigin)
{
    const float denom = dot(planeNormal, rayDirection);
    if (abs(denom) <= 1.0e-5f)
    {
        return planeOrigin;
    }

    const float t = dot(planeOrigin - rayOrigin, planeNormal) / denom;
    return rayOrigin + t * rayDirection;
}

bool CLodProjectWorldToShadowUv(float3 positionWS, Camera shadowCamera, out float2 shadowUv)
{
    const float4 shadowClip = mul(float4(positionWS, 1.0f), shadowCamera.viewProjection);
    const float safeW = max(abs(shadowClip.w), 1.0e-6f);
    const float3 projected = shadowClip.xyz / safeW;

    if (projected.x < -1.0f || projected.x > 1.0f ||
        projected.y < -1.0f || projected.y > 1.0f ||
        projected.z < 0.0f || projected.z > 1.0f)
    {
        shadowUv = 0.0f.xx;
        return false;
    }

    shadowUv = projected.xy * 0.5f + 0.5f;
    shadowUv.y = 1.0f - shadowUv.y;
    return true;
}

float4 CLodDirectionalShadowPageViewRow(Camera shadowCamera)
{
    return float4(
        shadowCamera.view[3][0],
        shadowCamera.view[3][1],
        shadowCamera.view[3][2],
        shadowCamera.view[3][3]);
}

    uint CLodDirectionalShadowPageViewInfoIndex(uint2 wrappedPageCoords, uint clipmapIndex)
    {
        return clipmapIndex * (kCLodVirtualShadowPageTableResolution * kCLodVirtualShadowPageTableResolution) +
        wrappedPageCoords.y * kCLodVirtualShadowPageTableResolution +
        wrappedPageCoords.x;
    }

void CLodMarkVirtualShadowWrappedPage(
    uint2 wrappedPageCoords,
    uint clipmapIndex,
    uint activeClipmapCount,
    uint frameIndex,
    float4 directionalPageViewRow,
    RWStructuredBuffer<uint4> allocationRequests,
    RWStructuredBuffer<uint> allocationCountBuffer,
    RWTexture2DArray<uint> pageTable,
    RWStructuredBuffer<uint> dirtyFlags,
    RWStructuredBuffer<uint4> pageMetadata,
    RWStructuredBuffer<float4> directionalPageViewInfo,
    RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer)
{
    const uint3 pageCoords = uint3(wrappedPageCoords, clipmapIndex);
    const uint pageViewInfoIndex = CLodDirectionalShadowPageViewInfoIndex(wrappedPageCoords, clipmapIndex);

    const uint currentPageState = pageTable[pageCoords];
    if ((currentPageState & kCLodVirtualShadowAllocatedMask) != 0u)
    {
        const uint physicalPageIndex = currentPageState & kCLodVirtualShadowPhysicalPageIndexMask;
        pageMetadata[physicalPageIndex].y = frameIndex;
        if ((currentPageState & kCLodVirtualShadowDirtyMask) != 0u)
        {
            CLodVirtualShadowStatsIncrementMarkResidentDirtyHit(statsBuffer, clipmapIndex);
            directionalPageViewInfo[pageViewInfoIndex] = directionalPageViewRow;
            InterlockedOr(dirtyFlags[physicalPageIndex >> 5u], 1u << (physicalPageIndex & 31u));
        }
        else
        {
            CLodVirtualShadowStatsIncrementMarkResidentCleanHit(statsBuffer, clipmapIndex);
        }
        return;
    }

    if ((currentPageState & kCLodVirtualShadowDirtyMask) != 0u)
    {
        return;
    }

    uint previousPageState = 0u;
    InterlockedOr(pageTable[pageCoords], kCLodVirtualShadowDirtyMask, previousPageState);
    if ((previousPageState & kCLodVirtualShadowAllocatedMask) != 0u)
    {
        const uint physicalPageIndex = previousPageState & kCLodVirtualShadowPhysicalPageIndexMask;
        pageMetadata[physicalPageIndex].y = frameIndex;
        directionalPageViewInfo[pageViewInfoIndex] = directionalPageViewRow;
        if ((previousPageState & kCLodVirtualShadowDirtyMask) != 0u)
        {
            InterlockedOr(dirtyFlags[physicalPageIndex >> 5u], 1u << (physicalPageIndex & 31u));
        }
        return;
    }

    if ((previousPageState & kCLodVirtualShadowDirtyMask) != 0u)
    {
        return;
    }

    uint requestIndex = 0u;
    InterlockedAdd(allocationCountBuffer[0], 1u, requestIndex);
    if (requestIndex >= CLOD_VIRTUAL_SHADOW_MARK_MAX_REQUEST_COUNT)
    {
        InterlockedAdd(statsBuffer[0].markRequestOverflowCount, 1u);
        uint ignored = 0u;
        InterlockedAnd(pageTable[pageCoords], ~kCLodVirtualShadowDirtyMask, ignored);
        return;
    }

    const uint virtualAddress =
        wrappedPageCoords.y * CLOD_VIRTUAL_SHADOW_MARK_PAGE_TABLE_RESOLUTION + wrappedPageCoords.x;
    allocationRequests[requestIndex] = uint4(
        virtualAddress,
        clipmapIndex,
        activeClipmapCount - clipmapIndex,
        0u);
    CLodVirtualShadowStatsIncrementRequestedPages(statsBuffer, clipmapIndex);
}

bool CLodIsVisibleClusterOwnedByReyes(uint visibleClusterIndex, uint ownershipDescriptorIndex)
{
    StructuredBuffer<uint> ownershipWords = ResourceDescriptorHeap[ownershipDescriptorIndex];
    const uint ownershipWord = ownershipWords[visibleClusterIndex >> 5u];
    return ((ownershipWord >> (visibleClusterIndex & 31u)) & 1u) != 0u;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void ClearReyesOwnershipBitsetCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint wordIndex = dispatchThreadId.x;
    if (wordIndex >= CLOD_REYES_RESET_OWNERSHIP_BITSET_WORD_COUNT)
    {
        return;
    }

    RWStructuredBuffer<uint> ownershipWords = ResourceDescriptorHeap[CLOD_REYES_RESET_OWNERSHIP_BITSET_DESCRIPTOR_INDEX];
    ownershipWords[wordIndex] = 0u;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void ClearReyesQueueCountersCSMain()
{
    RWStructuredBuffer<uint> fullClusterCounter = ResourceDescriptorHeap[CLOD_REYES_RESET_FULL_CLUSTER_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> ownedClusterCounter = ResourceDescriptorHeap[CLOD_REYES_RESET_OWNED_CLUSTER_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> splitQueueCounterA = ResourceDescriptorHeap[CLOD_REYES_RESET_SPLIT_QUEUE_COUNTER_A_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> splitQueueOverflowA = ResourceDescriptorHeap[CLOD_REYES_RESET_SPLIT_QUEUE_OVERFLOW_A_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> splitQueueCounterB = ResourceDescriptorHeap[CLOD_REYES_RESET_SPLIT_QUEUE_COUNTER_B_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> splitQueueOverflowB = ResourceDescriptorHeap[CLOD_REYES_RESET_SPLIT_QUEUE_OVERFLOW_B_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> diceQueueCounter = ResourceDescriptorHeap[CLOD_REYES_RESET_DICE_QUEUE_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> diceQueueOverflow = ResourceDescriptorHeap[CLOD_REYES_RESET_DICE_QUEUE_OVERFLOW_DESCRIPTOR_INDEX];

    fullClusterCounter[0] = 0u;
    ownedClusterCounter[0] = 0u;
    splitQueueCounterA[0] = 0u;
    splitQueueOverflowA[0] = 0u;
    splitQueueCounterB[0] = 0u;
    splitQueueOverflowB[0] = 0u;
    if (CLOD_REYES_RESET_CLEAR_DICE_QUEUE_COUNTER != 0u)
    {
        diceQueueCounter[0] = 0u;
    }
    diceQueueOverflow[0] = 0u;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void ClearReyesSplitOutputCountersCSMain()
{
    RWStructuredBuffer<uint> outputSplitQueueCounter = ResourceDescriptorHeap[CLOD_REYES_SPLIT_OUTPUT_SPLIT_QUEUE_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> outputSplitQueueOverflowCounter = ResourceDescriptorHeap[CLOD_REYES_SPLIT_OUTPUT_SPLIT_QUEUE_OVERFLOW_DESCRIPTOR_INDEX];

    outputSplitQueueCounter[0] = 0u;
    outputSplitQueueOverflowCounter[0] = 0u;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void BuildReyesDispatchArgsCSMain()
{
    StructuredBuffer<uint> sourceCounterBuffer = ResourceDescriptorHeap[CLOD_REYES_CREATE_DISPATCH_ARGS_SOURCE_COUNTER_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodReyesDispatchIndirectCommand> indirectArgsBuffer = ResourceDescriptorHeap[CLOD_REYES_CREATE_DISPATCH_ARGS_OUTPUT_DESCRIPTOR_INDEX];

    uint workItemCount = sourceCounterBuffer.Load(0);
    if (CLOD_REYES_CREATE_DISPATCH_ARGS_SOURCE_BASE_COUNTER_DESCRIPTOR_INDEX != 0xFFFFFFFFu)
    {
        StructuredBuffer<uint> sourceBaseCounterBuffer = ResourceDescriptorHeap[CLOD_REYES_CREATE_DISPATCH_ARGS_SOURCE_BASE_COUNTER_DESCRIPTOR_INDEX];
        const uint baseWorkItemCount = sourceBaseCounterBuffer.Load(0);
        workItemCount = (workItemCount > baseWorkItemCount) ? (workItemCount - baseWorkItemCount) : 0u;
    }
    const uint threadsPerGroup = max(CLOD_REYES_CREATE_DISPATCH_ARGS_THREADS_PER_GROUP, 1u);
    workItemCount = min(workItemCount, CLOD_REYES_CREATE_DISPATCH_ARGS_MAX_WORK_ITEM_COUNT);

    indirectArgsBuffer[0].dispatchX = (workItemCount + threadsPerGroup - 1u) / threadsPerGroup;
    indirectArgsBuffer[0].dispatchY = 1u;
    indirectArgsBuffer[0].dispatchZ = 1u;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodVirtualShadowClearPhysicalPagesCSMain(
    uint3 groupId : SV_GroupID,
    uint3 groupThreadId : SV_GroupThreadID)
{
    if (groupId.x >= kCLodVirtualShadowPhysicalPagesPerAxis * kCLodVirtualShadowPhysicalPagesPerAxis)
    {
        return;
    }

    RWTexture2D<uint> physicalPages = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_CLEAR_PHYSICAL_PAGES_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> dirtyFlags = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_FLAGS_DESCRIPTOR_INDEX];

    if (groupThreadId.x == 0u && groupThreadId.y == 0u)
    {
        const uint physicalPageIndex = groupId.x;
        const uint dirtyWordIndex = physicalPageIndex >> 5u;
        const uint dirtyBitMask = 1u << (physicalPageIndex & 31u);
        const uint dirtyWord = dirtyFlags[dirtyWordIndex];
        gCLodVirtualShadowShouldClearPage = (dirtyWord & dirtyBitMask) != 0u ? 1u : 0u;
        if (gCLodVirtualShadowShouldClearPage != 0u)
        {
            uint ignored = 0u;
            InterlockedAnd(dirtyFlags[dirtyWordIndex], ~dirtyBitMask, ignored);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (gCLodVirtualShadowShouldClearPage == 0u)
    {
        return;
    }

    const uint atlasPageX = groupId.x % kCLodVirtualShadowPhysicalPagesPerAxis;
    const uint atlasPageY = groupId.x / kCLodVirtualShadowPhysicalPagesPerAxis;
    const uint2 atlasBasePixel = uint2(
        atlasPageX * kCLodVirtualShadowPhysicalPageSize,
        atlasPageY * kCLodVirtualShadowPhysicalPageSize);

    for (uint localY = groupThreadId.y; localY < kCLodVirtualShadowPhysicalPageSize; localY += 8u)
    {
        for (uint localX = groupThreadId.x; localX < kCLodVirtualShadowPhysicalPageSize; localX += 8u)
        {
            physicalPages[atlasBasePixel + uint2(localX, localY)] = 0xFFFFFFFFu;
        }
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodVirtualShadowSetupCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.z >= CLOD_VIRTUAL_SHADOW_SETUP_CLIPMAP_COUNT ||
        dispatchThreadId.x >= CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_RESOLUTION ||
        dispatchThreadId.y >= CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_RESOLUTION)
    {
        return;
    }

    RWTexture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint4> pageMetadata = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_SETUP_PAGE_METADATA_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> allocationCount = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_SETUP_ALLOCATION_COUNT_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> dirtyFlags = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_SETUP_DIRTY_FLAGS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_SETUP_STATS_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodVirtualShadowClipmapInfo> clipmapInfos = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_SETUP_CLIPMAP_INFO_DESCRIPTOR_INDEX];

    const CLodVirtualShadowClipmapInfo dispatchClipmapInfo = clipmapInfos[dispatchThreadId.z];
    const bool clearWrappedPage = CLodVirtualShadowShouldClearWrappedPage(
        int2(CLodVirtualShadowUnwrappedPageCoords(dispatchThreadId.xy, dispatchClipmapInfo)),
        dispatchClipmapInfo.clearOffsetX,
        dispatchClipmapInfo.clearOffsetY,
        CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_RESOLUTION);

    const uint existingPageEntry = pageTable[dispatchThreadId];

    if (CLOD_VIRTUAL_SHADOW_SETUP_RESET_RESOURCES != 0u || clearWrappedPage)
    {
        if (clearWrappedPage)
        {
            CLodVirtualShadowStatsIncrementSetupWrappedClear(statsBuffer, dispatchThreadId.z);
            if ((existingPageEntry & kCLodVirtualShadowAllocatedMask) != 0u)
            {
                const uint physicalPageIndex = existingPageEntry & kCLodVirtualShadowPhysicalPageIndexMask;
                if (physicalPageIndex < CLOD_VIRTUAL_SHADOW_SETUP_PHYSICAL_PAGE_COUNT)
                {
                    pageMetadata[physicalPageIndex] = uint4(0u, 0u, 0u, 0u);
                }
            }
        }
        pageTable[dispatchThreadId] = 0u;
    }
    else if ((existingPageEntry & kCLodVirtualShadowDirtyMask) != 0u)
    {
        CLodVirtualShadowStatsIncrementSetupStaleDirtyClear(statsBuffer, dispatchThreadId.z);
        pageTable[dispatchThreadId] = existingPageEntry & ~kCLodVirtualShadowDirtyMask;
    }

    if (dispatchThreadId.z != 0u)
    {
        return;
    }

    const uint linearIndex = dispatchThreadId.y * CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_RESOLUTION + dispatchThreadId.x;
    if (linearIndex == 0u)
    {
        statsBuffer[0] = (CLodVirtualShadowStats)0;
        if (CLOD_VIRTUAL_SHADOW_SETUP_RESET_RESOURCES != 0u)
        {
            statsBuffer[0].setupResetApplied = 1u;
        }
        statsBuffer[0].setupResetForced = CLOD_VIRTUAL_SHADOW_SETUP_RESET_REASON_FORCED;
        statsBuffer[0].setupResetNoPreviousState = CLOD_VIRTUAL_SHADOW_SETUP_RESET_REASON_NO_PREVIOUS_STATE;
        statsBuffer[0].setupResetStructureMismatch = CLOD_VIRTUAL_SHADOW_SETUP_RESET_REASON_STRUCTURE_MISMATCH;
    }
    if (linearIndex < CLOD_VIRTUAL_SHADOW_SETUP_PHYSICAL_PAGE_COUNT)
    {
        if (CLOD_VIRTUAL_SHADOW_SETUP_RESET_RESOURCES != 0u)
        {
            pageMetadata[linearIndex] = uint4(0u, 0u, 0u, 0u);
        }
        else
        {
            const uint4 meta = pageMetadata[linearIndex];
            if ((meta.z & kCLodVirtualShadowPhysicalPageResidentFlag) != 0u &&
                meta.w < CLOD_VIRTUAL_SHADOW_SETUP_CLIPMAP_COUNT)
            {
                const CLodVirtualShadowClipmapInfo ownerClipmapInfo = clipmapInfos[meta.w];
                const uint ownerVirtualAddress = meta.x;
                const uint2 ownerWrappedPageCoords = uint2(
                    ownerVirtualAddress % CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_RESOLUTION,
                    ownerVirtualAddress / CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_RESOLUTION);
                const uint2 ownerLogicalPageCoords = CLodVirtualShadowUnwrappedPageCoords(
                    ownerWrappedPageCoords,
                    ownerClipmapInfo);
                if (CLodVirtualShadowShouldClearWrappedPage(
                        int2(ownerLogicalPageCoords),
                        ownerClipmapInfo.clearOffsetX,
                        ownerClipmapInfo.clearOffsetY,
                        CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_RESOLUTION))
                {
                    pageMetadata[linearIndex] = uint4(0u, 0u, 0u, 0u);
                }
            }
        }
    }

    if (linearIndex < CLOD_VIRTUAL_SHADOW_SETUP_DIRTY_WORD_COUNT)
    {
        dirtyFlags[linearIndex] = 0u;
    }

    if (linearIndex == 0u)
    {
        allocationCount[0] = 0u;
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodVirtualShadowMarkPagesCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= CLOD_VIRTUAL_SHADOW_MARK_SCREEN_WIDTH ||
        dispatchThreadId.y >= CLOD_VIRTUAL_SHADOW_MARK_SCREEN_HEIGHT)
    {
        return;
    }

    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Texture2D<float> depthTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::LinearDepthMap)];
    Texture2D<float4> normalsTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::GBuffer::Normals)];
    StructuredBuffer<CLodVirtualShadowClipmapInfo> clipmapInfos = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_CLIPMAP_INFO_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint4> allocationRequests = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_REQUESTS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> allocationCountBuffer = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_REQUEST_COUNT_DESCRIPTOR_INDEX];
    RWTexture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_PAGE_TABLE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> dirtyFlags = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_DIRTY_FLAGS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint4> pageMetadata = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_PAGE_METADATA_DESCRIPTOR_INDEX];
    RWStructuredBuffer<float4> directionalPageViewInfo = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_PAGE_VIEW_INFO_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_STATS_DESCRIPTOR_INDEX];

    const uint2 pixel = dispatchThreadId.xy;
    const float linearDepth = depthTexture[pixel];
    if (asuint(linearDepth) == 0x7F7FFFFF)
    {
        return;
    }

    const float2 invScreenSize = rcp(float2(CLOD_VIRTUAL_SHADOW_MARK_SCREEN_WIDTH, CLOD_VIRTUAL_SHADOW_MARK_SCREEN_HEIGHT));
    const float2 uv = (float2(pixel) + 0.5f) * invScreenSize;

    const Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    const float3 positionWS = CLodWorldSpaceFromScreenUv(uv, linearDepth, mainCamera);

    float3 normalWS = normalsTexture.Load(int3(pixel, 0)).xyz;
    const float normalLengthSq = dot(normalWS, normalWS);
    if (normalLengthSq <= 1.0e-6f)
    {
        normalWS = normalize(mainCamera.positionWorldSpace.xyz - positionWS);
    }
    else
    {
        normalWS = normalize(normalWS);
    }

    const float2 uvOffset = 0.5f * invScreenSize;
    const float2 sampleUvBR = saturate(uv + float2(uvOffset.x, uvOffset.y));
    const float2 sampleUvBL = saturate(uv + float2(-uvOffset.x, uvOffset.y));
    const float2 sampleUvTR = saturate(uv + float2(uvOffset.x, -uvOffset.y));
    const float2 sampleUvTL = saturate(uv + float2(-uvOffset.x, -uvOffset.y));

    const float3 sampleWsBR = CLodWorldSpaceFromScreenUv(sampleUvBR, linearDepth, mainCamera);
    const float3 sampleWsBL = CLodWorldSpaceFromScreenUv(sampleUvBL, linearDepth, mainCamera);
    const float3 sampleWsTR = CLodWorldSpaceFromScreenUv(sampleUvTR, linearDepth, mainCamera);
    const float3 sampleWsTL = CLodWorldSpaceFromScreenUv(sampleUvTL, linearDepth, mainCamera);

    const float3 rayOrigin = mainCamera.positionWorldSpace.xyz;
    const float3 footprintBR = CLodRayPlaneIntersection(normalize(sampleWsBR - rayOrigin), rayOrigin, normalWS, positionWS);
    const float3 footprintBL = CLodRayPlaneIntersection(normalize(sampleWsBL - rayOrigin), rayOrigin, normalWS, positionWS);
    const float3 footprintTR = CLodRayPlaneIntersection(normalize(sampleWsTR - rayOrigin), rayOrigin, normalWS, positionWS);
    const float3 footprintTL = CLodRayPlaneIntersection(normalize(sampleWsTL - rayOrigin), rayOrigin, normalWS, positionWS);

    const uint activeClipmapCount = min(perFrameBuffer.numShadowCascades, CLOD_VIRTUAL_SHADOW_MARK_CLIPMAP_COUNT);
    if (activeClipmapCount == 0u)
    {
        return;
    }

    const float footprintWidth = length(footprintBR - footprintBL);
    const float footprintHeight = length(footprintTR - footprintTL);
    const float footprintDiameter = max(max(footprintWidth, footprintHeight), 1.0e-5f);
    const float baseTexelWorldSize = max(clipmapInfos[0].texelWorldSize, 1.0e-5f);
    const float footprintScale = max(footprintDiameter / baseTexelWorldSize, 1.0f);
    const uint footprintClipmapIndex = min(
        (uint)ceil(log2(footprintScale)),
        activeClipmapCount - 1u);

    const uint cascadeClipmapIndex = min(
        CLodCalculateShadowCascadeIndex(linearDepth, activeClipmapCount, perFrameBuffer.shadowCascadeSplits),
        activeClipmapCount - 1u);
    const uint clipmapIndex = max(cascadeClipmapIndex, footprintClipmapIndex);

    const CLodVirtualShadowClipmapInfo clipmapInfo = clipmapInfos[clipmapIndex];
    if (!CLodVirtualShadowClipmapIsValid(clipmapInfo))
    {
        return;
    }

    CLodVirtualShadowStatsIncrementSelected(statsBuffer, clipmapIndex);

    const Camera shadowCamera = cameras[clipmapInfo.shadowCameraBufferIndex];
    const float4 directionalPageViewRow = CLodDirectionalShadowPageViewRow(shadowCamera);
    float2 shadowUvCenter;
    bool validCenter = CLodProjectWorldToShadowUv(positionWS, shadowCamera, shadowUvCenter);
    float2 shadowUvBR;
    bool validBR = CLodProjectWorldToShadowUv(footprintBR, shadowCamera, shadowUvBR);
    float2 shadowUvBL;
    bool validBL = CLodProjectWorldToShadowUv(footprintBL, shadowCamera, shadowUvBL);
    float2 shadowUvTR;
    bool validTR = CLodProjectWorldToShadowUv(footprintTR, shadowCamera, shadowUvTR);
    float2 shadowUvTL;
    bool validTL = CLodProjectWorldToShadowUv(footprintTL, shadowCamera, shadowUvTL);

    if (!validCenter && !validBR && !validBL && !validTR && !validTL)
    {
        CLodVirtualShadowStatsIncrementProjectionRejected(statsBuffer, clipmapIndex);
        return;
    }

    uint2 markedPages[5];
    [unroll]
    for (uint initIndex = 0u; initIndex < 5u; ++initIndex)
    {
        markedPages[initIndex] = uint2(0xFFFFFFFFu, 0xFFFFFFFFu);
    }
    uint markedPageCount = 0u;

#define CLOD_MARK_FOOTPRINT_PAGE(validFlag, uvValue) \
    if (validFlag) { \
        const uint2 wrappedPageCoords = CLodVirtualShadowWrappedPageCoords( \
            CLodVirtualShadowVirtualPageCoordsFromUv(uvValue), \
            clipmapInfo); \
        bool alreadyMarked = false; \
        [unroll] \
        for (uint markedIndex = 0u; markedIndex < 5u; ++markedIndex) { \
            if (markedIndex >= markedPageCount) { break; } \
            if (all(markedPages[markedIndex] == wrappedPageCoords)) { \
                alreadyMarked = true; \
                break; \
            } \
        } \
        if (!alreadyMarked && markedPageCount < 5u) { \
            markedPages[markedPageCount] = wrappedPageCoords; \
            markedPageCount += 1u; \
            CLodMarkVirtualShadowWrappedPage( \
                wrappedPageCoords, \
                clipmapInfo.pageTableLayer, \
                activeClipmapCount, \
                perFrameBuffer.frameIndex, \
                directionalPageViewRow, \
                allocationRequests, \
                allocationCountBuffer, \
                pageTable, \
                dirtyFlags, \
                pageMetadata, \
                directionalPageViewInfo, \
                statsBuffer); \
        } \
    }

    CLOD_MARK_FOOTPRINT_PAGE(validCenter, shadowUvCenter);
    CLOD_MARK_FOOTPRINT_PAGE(validBR, shadowUvBR);
    CLOD_MARK_FOOTPRINT_PAGE(validBL, shadowUvBL);
    CLOD_MARK_FOOTPRINT_PAGE(validTR, shadowUvTR);
    CLOD_MARK_FOOTPRINT_PAGE(validTL, shadowUvTL);

#undef CLOD_MARK_FOOTPRINT_PAGE

}

[shader("compute")]
[numthreads(64, 1, 1)]
void CLodVirtualShadowBuildPageListsCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<uint4> pageMetadata = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_PAGE_METADATA_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> freePhysicalPages = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_FREE_PAGES_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> reusablePhysicalPages = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_REUSABLE_PAGES_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint4> pageListHeader = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_HEADER_DESCRIPTOR_INDEX];

    if (dispatchThreadId.x == 0u)
    {
        pageListHeader[0] = uint4(0u, 0u, 0u, 0u);
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint physicalPageIndex = dispatchThreadId.x;
        physicalPageIndex < CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_PHYSICAL_PAGE_COUNT;
        physicalPageIndex += 64u)
    {
        const uint4 meta = pageMetadata[physicalPageIndex];
        const uint metaFlags = meta.z;
        if ((metaFlags & kCLodVirtualShadowPhysicalPageResidentFlag) == 0u)
        {
            uint freeIndex = 0u;
            InterlockedAdd(pageListHeader[0].x, 1u, freeIndex);
            if (freeIndex < CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_PHYSICAL_PAGE_COUNT)
            {
                freePhysicalPages[freeIndex] = physicalPageIndex;
            }

            continue;
        }

        if (meta.y != perFrameBuffer.frameIndex)
        {
            uint reusableIndex = 0u;
            InterlockedAdd(pageListHeader[0].y, 1u, reusableIndex);
            if (reusableIndex < CLOD_VIRTUAL_SHADOW_BUILD_PAGE_LISTS_PHYSICAL_PAGE_COUNT)
            {
                reusablePhysicalPages[reusableIndex] = physicalPageIndex;
            }
        }
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void CLodVirtualShadowAllocatePagesCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    StructuredBuffer<uint4> allocationRequests = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_REQUESTS_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> allocationCountBuffer = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_REQUEST_COUNT_DESCRIPTOR_INDEX];
    RWTexture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_TABLE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint4> pageMetadata = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_METADATA_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> dirtyFlags = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_DIRTY_FLAGS_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> freePhysicalPages = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_FREE_PAGES_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> reusablePhysicalPages = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_REUSABLE_PAGES_DESCRIPTOR_INDEX];
    StructuredBuffer<uint4> pageListHeader = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_LIST_HEADER_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodVirtualShadowClipmapInfo> clipmapInfos = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_CLIPMAP_INFO_DESCRIPTOR_INDEX];
    RWStructuredBuffer<float4> directionalPageViewInfo = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_VIEW_INFO_DESCRIPTOR_INDEX];

    const uint requestIndex = dispatchThreadId.x;
    const uint requestCount = allocationCountBuffer.Load(0);
    if (requestIndex >= requestCount)
    {
        return;
    }

    const uint freePageCount = min(pageListHeader[0].x, CLOD_VIRTUAL_SHADOW_ALLOCATE_PHYSICAL_PAGE_COUNT);
    const uint reusablePageCount = min(pageListHeader[0].y, CLOD_VIRTUAL_SHADOW_ALLOCATE_PHYSICAL_PAGE_COUNT);

    uint selectedPhysicalPageIndex = CLOD_VIRTUAL_SHADOW_ALLOCATE_PHYSICAL_PAGE_COUNT;
    if (requestIndex < freePageCount)
    {
        selectedPhysicalPageIndex = freePhysicalPages[requestIndex];
    }
    else if ((requestIndex - freePageCount) < reusablePageCount)
    {
        selectedPhysicalPageIndex = reusablePhysicalPages[requestIndex - freePageCount];
    }
    else
    {
        return;
    }

    const uint4 request = allocationRequests[requestIndex];
    const uint virtualAddress = request.x;
    const uint clipmapIndex = request.y;
    if (clipmapIndex >= CLOD_VIRTUAL_SHADOW_ALLOCATE_CLIPMAP_COUNT)
    {
        return;
    }

    const uint pageX = virtualAddress % CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_TABLE_RESOLUTION;
    const uint pageY = virtualAddress / CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_TABLE_RESOLUTION;
    if (pageY >= CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_TABLE_RESOLUTION ||
        selectedPhysicalPageIndex >= CLOD_VIRTUAL_SHADOW_ALLOCATE_PHYSICAL_PAGE_COUNT)
    {
        return;
    }

    const uint4 previousMeta = pageMetadata[selectedPhysicalPageIndex];
    if ((previousMeta.z & kCLodVirtualShadowPhysicalPageResidentFlag) != 0u &&
        (previousMeta.x != virtualAddress || previousMeta.w != clipmapIndex))
    {
        const uint previousVirtualAddress = previousMeta.x;
        const uint previousClipmapIndex = previousMeta.w;
        const uint previousPageX = previousVirtualAddress % CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_TABLE_RESOLUTION;
        const uint previousPageY = previousVirtualAddress / CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_TABLE_RESOLUTION;
        if (previousPageY < CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_TABLE_RESOLUTION &&
            previousClipmapIndex < CLOD_VIRTUAL_SHADOW_ALLOCATE_CLIPMAP_COUNT)
        {
            pageTable[uint3(previousPageX, previousPageY, previousClipmapIndex)] = 0u;
        }
    }

    const uint pageEntry = selectedPhysicalPageIndex | kCLodVirtualShadowAllocatedMask | kCLodVirtualShadowDirtyMask;
    pageTable[uint3(pageX, pageY, clipmapIndex)] = pageEntry;
    pageMetadata[selectedPhysicalPageIndex] = uint4(
        virtualAddress,
        perFrameBuffer.frameIndex,
        kCLodVirtualShadowPhysicalPageResidentFlag,
        clipmapIndex);
    const Camera shadowCamera = cameras[clipmapInfos[clipmapIndex].shadowCameraBufferIndex];
    directionalPageViewInfo[CLodDirectionalShadowPageViewInfoIndex(uint2(pageX, pageY), clipmapIndex)] = CLodDirectionalShadowPageViewRow(shadowCamera);
    InterlockedOr(dirtyFlags[selectedPhysicalPageIndex >> 5u], 1u << (selectedPhysicalPageIndex & 31u));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodVirtualShadowBuildDirtyHierarchyCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.z >= CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_CLIPMAP_COUNT)
    {
        return;
    }

    const uint dstResolution = (CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_IS_PAGE_TABLE != 0u)
        ? CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_RESOLUTION
        : max(CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_RESOLUTION >> 1u, 1u);
    if (dispatchThreadId.x >= dstResolution || dispatchThreadId.y >= dstResolution)
    {
        return;
    }

    Texture2DArray<uint> sourceTexture = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_DESCRIPTOR_INDEX];
    RWTexture2DArray<uint> destTexture = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_DEST_DESCRIPTOR_INDEX];
    uint dirtyValue = 0u;

    if (CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_IS_PAGE_TABLE != 0u)
    {
        const uint srcValue = sourceTexture.Load(int4(dispatchThreadId.xy, dispatchThreadId.z, 0));
        dirtyValue = ((srcValue & kCLodVirtualShadowDirtyMask) != 0u) ? 1u : 0u;
    }
    else
    {
        const uint2 srcBase = dispatchThreadId.xy * 2u;
        [unroll]
        for (uint offsetY = 0u; offsetY < 2u; ++offsetY)
        {
            [unroll]
            for (uint offsetX = 0u; offsetX < 2u; ++offsetX)
            {
                const uint srcX = min(srcBase.x + offsetX, CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_RESOLUTION - 1u);
                const uint srcY = min(srcBase.y + offsetY, CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_RESOLUTION - 1u);
                const uint srcValue = sourceTexture.Load(int4(srcX, srcY, dispatchThreadId.z, 0));
                dirtyValue = max(dirtyValue, srcValue != 0u ? 1u : 0u);
            }
        }
    }

    destTexture[dispatchThreadId] = dirtyValue;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodVirtualShadowClearDirtyBitsCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.z >= CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_CLIPMAP_COUNT ||
        dispatchThreadId.x >= CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_PAGE_TABLE_RESOLUTION ||
        dispatchThreadId.y >= CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_PAGE_TABLE_RESOLUTION)
    {
        return;
    }

    RWTexture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_CLEAR_DIRTY_BITS_PAGE_TABLE_DESCRIPTOR_INDEX];
    const uint pageEntry = pageTable[dispatchThreadId];
    pageTable[dispatchThreadId] = pageEntry & ~kCLodVirtualShadowDirtyMask;
}

uint CLodGetHistogramVisibleClusterReadIndex(uint linearizedID)
{
    uint readBase = 0u;
    if (CLOD_HISTOGRAM_READ_BASE_COUNTER_DESCRIPTOR_INDEX != 0xFFFFFFFFu)
    {
        StructuredBuffer<uint> readBaseCounter = ResourceDescriptorHeap[CLOD_HISTOGRAM_READ_BASE_COUNTER_DESCRIPTOR_INDEX];
        readBase = readBaseCounter.Load(0);
    }

    if ((CLOD_HISTOGRAM_READ_MODE_FLAGS & CLOD_HISTOGRAM_READ_FLAG_REVERSED) != 0u)
    {
        return CLOD_HISTOGRAM_READ_CAPACITY - 1u - (readBase + linearizedID);
    }

    return readBase + linearizedID;
}

uint CLodGetCompactionVisibleClusterReadIndex(uint linearizedID)
{
    uint readBase = 0u;
    if (CLOD_COMPACTION_READ_BASE_COUNTER_DESCRIPTOR_INDEX != 0xFFFFFFFFu)
    {
        StructuredBuffer<uint> readBaseCounter = ResourceDescriptorHeap[CLOD_COMPACTION_READ_BASE_COUNTER_DESCRIPTOR_INDEX];
        readBase = readBaseCounter.Load(0);
    }

    if ((CLOD_COMPACTION_READ_MODE_FLAGS & CLOD_COMPACTION_READ_FLAG_REVERSED) != 0u)
    {
        return CLOD_COMPACTION_READ_CAPACITY - 1u - (readBase + linearizedID);
    }

    return readBase + linearizedID;
}

#define CLUSTER_HISTOGRAM_GROUP_SIZE 8

// Single-thread shader to create a command for histogram eval
[shader("compute")]
[numthreads(1, 1, 1)]
void CreateRasterBucketsHistogramCommandCSMain()
{
    RWStructuredBuffer<RasterBucketsHistogramIndirectCommand> outCommand = ResourceDescriptorHeap[CLOD_CREATE_RASTER_BUCKET_HISTOGRAM_COMMAND_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> clusterCountBuffer = ResourceDescriptorHeap[CLOD_CREATE_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];

    // Given the cluster count, find dispatch dimensions that minimizes wasted threads
    uint clusterCount = clusterCountBuffer.Load(0);
    uint numBuckets = CLOD_CREATE_NUM_RASTER_BUCKETS;
    uint totalItems = max(clusterCount, numBuckets);

    uint groupsNeeded = (totalItems + CLUSTER_HISTOGRAM_GROUP_SIZE - 1u) / CLUSTER_HISTOGRAM_GROUP_SIZE;

    const uint kMaxDim = 65535u;
    uint dispatchX = (uint) ceil(sqrt((float) groupsNeeded));
    if (dispatchX > kMaxDim)
    {
        dispatchX = kMaxDim;
    }

    uint dispatchY = (groupsNeeded + dispatchX - 1u) / dispatchX;
    if (dispatchY > kMaxDim)
    {
        dispatchY = kMaxDim;
    }

    outCommand[0].clusterCount = clusterCount;
    outCommand[0].xDim = dispatchX;
    outCommand[0].dispatchX = dispatchX;
    outCommand[0].dispatchY = dispatchY;
    outCommand[0].dispatchZ = 1;

    StructuredBuffer<CLodReplayBufferState> replayStateBuffer = ResourceDescriptorHeap[CLOD_CREATE_OCCLUSION_REPLAY_STATE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodNodeGpuInput> nodeInputs = ResourceDescriptorHeap[CLOD_CREATE_WORKGRAPH_NODE_INPUTS_DESCRIPTOR_INDEX];

    const CLodReplayBufferState replayState = replayStateBuffer[0];

    const uint nodeReplayCount = min(replayState.nodeWriteCount, CLOD_NODE_REPLAY_CAPACITY);
    const uint meshletReplayCount = min(replayState.meshletWriteCount, CLOD_MESHLET_REPLAY_CAPACITY);

    // Slot 0 is CLodMultiNodeGpuInput (CPU initialized); slot 1+ are CLodNodeGpuInput records.
    // Entry point 1 = TraverseNodes (node replay region, 12-byte stride).
    // Entry point 2 = ClusterCull1  (meshlet replay region, 24-byte stride).
    // recordsAddress for meshlet region is patched by C++ to include CLOD_REPLAY_MESHLET_REGION_OFFSET.
    nodeInputs[1].numRecords = nodeReplayCount;
    nodeInputs[1].recordStride = CLOD_NODE_REPLAY_STRIDE_BYTES;
    nodeInputs[2].numRecords = meshletReplayCount;
    nodeInputs[2].recordStride = CLOD_MESHLET_REPLAY_STRIDE_BYTES;
}

// IndirectCommandSignatureRootConstant0 = cluster count
// IndirectCommandSignatureRootConstant1 = xDim
[shader("compute")]
[numthreads(CLUSTER_HISTOGRAM_GROUP_SIZE, 1, 1)]
void ClusterRasterBucketsHistogramCSMain(uint3 DTid : SV_DispatchThreadID)
{
    // Linearize the 2D dispatch thread ID
    // Root constant is dispatchX in groups, convert to thread width.
    uint xDimThreads = IndirectCommandSignatureRootConstant1 * CLUSTER_HISTOGRAM_GROUP_SIZE;
    uint linearizedID = DTid.x + DTid.y * xDimThreads;
    StructuredBuffer<uint> clusterCountBuffer = ResourceDescriptorHeap[CLOD_HISTOGRAM_VISIBLE_CLUSTERS_COUNTER_DESCRIPTOR_INDEX];
    uint clusterCount = clusterCountBuffer.Load(0);
    
    if (linearizedID >= clusterCount) {
        return;
    }

    const uint visibleClusterReadIndex = CLodGetHistogramVisibleClusterReadIndex(linearizedID);
    if ((CLOD_HISTOGRAM_READ_MODE_FLAGS & CLOD_HISTOGRAM_READ_FLAG_SKIP_REYES_OWNED) != 0u &&
        CLodIsVisibleClusterOwnedByReyes(visibleClusterReadIndex, CLOD_HISTOGRAM_REYES_OWNERSHIP_BITSET_DESCRIPTOR_INDEX)) {
        return;
    }

    ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[CLOD_HISTOGRAM_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];

    // TODO: Remove load chain
    uint instanceIndex = CLodVisibleClusterInstanceID(CLodLoadVisibleClusterPacked(visibleClusters, visibleClusterReadIndex));
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstance = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    uint perMeshIndex = perMeshInstance[instanceIndex].perMeshBufferIndex;
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    uint materialDataIndex = perMeshBuffer[perMeshIndex].materialDataIndex;
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];

    uint rasterBucketIndex = materialDataBuffer[materialDataIndex].rasterBucketIndex;

    // Group threads in the wave by matId
    uint4 mask = WaveMatch(rasterBucketIndex);

    if (IsWaveGroupLeader(mask))
    {
        uint groupSize = CountBits128(mask);
        RWStructuredBuffer<uint> histogramBuffer = ResourceDescriptorHeap[CLOD_HISTOGRAM_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX];
        InterlockedAdd(histogramBuffer[rasterBucketIndex], groupSize);
    }
}

// Prefix sum for raster bucket histogram

static const uint CLOD_BUCKET_BLOCK_SIZE = 1024; // must be power-of-two
static const uint CLOD_BUCKET_SCAN_THREADS = 256;

groupshared uint s_bucketData[CLOD_BUCKET_BLOCK_SIZE];
groupshared uint s_bucketScan[CLOD_BUCKET_BLOCK_SIZE];
groupshared uint s_bucketBlockSum;

void CLODExclusiveScanBlock(uint Nblock, uint threadId)
{
    if (Nblock == 0)
    {
        return;
    }

    uint P = 1;
    while (P < Nblock)
    {
        P <<= 1;
    }

    for (uint i = threadId; i < Nblock; i += CLOD_BUCKET_SCAN_THREADS)
    {
        s_bucketScan[i] = s_bucketData[i];
    }
    for (uint i = threadId + Nblock; i < P; i += CLOD_BUCKET_SCAN_THREADS)
    {
        s_bucketScan[i] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 1; stride < P; stride <<= 1)
    {
        uint idx = (threadId + 1) * (stride << 1) - 1;
        if (idx < P)
        {
            s_bucketScan[idx] += s_bucketScan[idx - stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (threadId == 0)
    {
        s_bucketScan[P - 1] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = (P >> 1); stride >= 1; stride >>= 1)
    {
        uint idx = (threadId + 1) * (stride << 1) - 1;
        if (idx < P)
        {
            uint t = s_bucketScan[idx - stride];
            s_bucketScan[idx - stride] = s_bucketScan[idx];
            s_bucketScan[idx] += t;
        }
        GroupMemoryBarrierWithGroupSync();

        if (stride == 1)
            break;
    }
}

// Root constants:
// UintRootConstant0 = NumBuckets
[numthreads(CLOD_BUCKET_SCAN_THREADS, 1, 1)]
void RasterBucketsBlockScanCS(uint3 groupThreadId : SV_GroupThreadID,
                              uint3 groupId : SV_GroupID)
{
    StructuredBuffer<uint> histogram = ResourceDescriptorHeap[CLOD_PREFIX_SCAN_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> offsets = ResourceDescriptorHeap[CLOD_PREFIX_SCAN_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> blockSums = ResourceDescriptorHeap[CLOD_PREFIX_SCAN_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX];

    uint N = CLOD_PREFIX_SCAN_NUM_BUCKETS;
    uint blockId = groupId.x;
    uint localTid = groupThreadId.x;
    uint base = blockId * CLOD_BUCKET_BLOCK_SIZE;

    for (uint i = localTid; i < CLOD_BUCKET_BLOCK_SIZE; i += CLOD_BUCKET_SCAN_THREADS)
    {
        uint globalIdx = base + i;
        s_bucketData[i] = (globalIdx < N) ? histogram[globalIdx] : 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint Nblock = 0;
    if (N > base)
        Nblock = min(CLOD_BUCKET_BLOCK_SIZE, N - base);

    CLODExclusiveScanBlock(Nblock, localTid);

    for (uint i = localTid; i < Nblock; i += CLOD_BUCKET_SCAN_THREADS)
    {
        offsets[base + i] = s_bucketScan[i];
    }

    uint localSum = 0;
    for (uint i = localTid; i < Nblock; i += CLOD_BUCKET_SCAN_THREADS)
    {
        localSum += s_bucketData[i];
    }

    if (localTid == 0)
        s_bucketBlockSum = 0;
    GroupMemoryBarrierWithGroupSync();

    InterlockedAdd(s_bucketBlockSum, localSum);
    GroupMemoryBarrierWithGroupSync();

    if (localTid == 0)
    {
        uint numBlocks = (N + CLOD_BUCKET_BLOCK_SIZE - 1) / CLOD_BUCKET_BLOCK_SIZE;
        if (blockId < numBlocks)
        {
            blockSums[blockId] = s_bucketBlockSum;
        }
    }
}

// Root constants:
// UintRootConstant0 = NumBuckets
// UintRootConstant1 = NumBlocks
[numthreads(CLOD_BUCKET_SCAN_THREADS, 1, 1)]
void RasterBucketsBlockOffsetsCS(uint3 groupThreadId : SV_GroupThreadID,
                                 uint3 groupId : SV_GroupID)
{
    if (groupId.x != 0 || groupId.y != 0 || groupId.z != 0)
        return;

    RWStructuredBuffer<uint> offsets = ResourceDescriptorHeap[CLOD_PREFIX_OFFSETS_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> blockSums = ResourceDescriptorHeap[CLOD_PREFIX_OFFSETS_RASTER_BUCKETS_BLOCK_SUMS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> scannedBlockSums = ResourceDescriptorHeap[CLOD_PREFIX_OFFSETS_RASTER_BUCKETS_SCANNED_BLOCK_SUMS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> totalOut = ResourceDescriptorHeap[CLOD_PREFIX_OFFSETS_RASTER_BUCKETS_TOTAL_COUNT_DESCRIPTOR_INDEX];

    uint N = CLOD_PREFIX_OFFSETS_NUM_BUCKETS;
    uint B = CLOD_PREFIX_OFFSETS_NUM_BLOCKS;

    uint localTid = groupThreadId.x;

    for (uint i = localTid; i < B; i += CLOD_BUCKET_SCAN_THREADS)
    {
        s_bucketData[i] = blockSums[i];
    }
    GroupMemoryBarrierWithGroupSync();

    CLODExclusiveScanBlock(B, localTid);

    for (uint i = localTid; i < B; i += CLOD_BUCKET_SCAN_THREADS)
    {
        scannedBlockSums[i] = s_bucketScan[i];
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint elem = localTid; elem < N; elem += CLOD_BUCKET_SCAN_THREADS)
    {
        uint blockId = elem / CLOD_BUCKET_BLOCK_SIZE;
        uint prefix = s_bucketScan[blockId];
        offsets[elem] += prefix;
    }

    if (localTid == 0 && B > 0)
    {
        uint total = s_bucketScan[B - 1] + s_bucketData[B - 1];
        totalOut[0] = total;
    }
}

uint GetRasterBucketIndexFromInstance(uint instanceID)
{
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstance = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    StructuredBuffer<MaterialInfo> materialDataBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMaterialDataBuffer)];

    PerMeshInstanceBuffer instanceData = perMeshInstance[instanceID];
    PerMeshBuffer meshBuffer = perMeshBuffer[instanceData.perMeshBufferIndex];
    MaterialInfo materialInfo = materialDataBuffer[meshBuffer.materialDataIndex];

    return materialInfo.rasterBucketIndex;
}

static const uint CLOD_COMPACTION_GROUP_SIZE = CLUSTER_HISTOGRAM_GROUP_SIZE;

[shader("compute")]
[numthreads(CLOD_COMPACTION_GROUP_SIZE, 1, 1)]
void CompactClustersAndBuildIndirectArgsCS(uint3 dtid : SV_DispatchThreadID)
{
    uint xDimThreads = IndirectCommandSignatureRootConstant1 * CLOD_COMPACTION_GROUP_SIZE;
    uint linearizedID = dtid.x + dtid.y * xDimThreads;

    uint clusterCount = IndirectCommandSignatureRootConstant0;

    StructuredBuffer<uint> histogram = ResourceDescriptorHeap[CLOD_COMPACTION_RASTER_BUCKETS_HISTOGRAM_DESCRIPTOR_INDEX];
    const uint numBucketsPacked = CLOD_COMPACTION_NUM_RASTER_BUCKETS;
    const bool appendToExisting = ((numBucketsPacked & 0x80000000u) != 0u);
    const uint numBuckets = (numBucketsPacked & 0x7FFFFFFFu);

    uint baseClusterOffset = 0u;
    if (appendToExisting)
    {
        StructuredBuffer<uint> appendBaseCounter = ResourceDescriptorHeap[CLOD_COMPACTION_APPEND_BASE_COUNTER_DESCRIPTOR_INDEX];
        baseClusterOffset = appendBaseCounter.Load(0);
    }

    if (linearizedID < clusterCount)
    {
        const uint sourceClusterIndex = CLodGetCompactionVisibleClusterReadIndex(linearizedID);
        if ((CLOD_COMPACTION_READ_MODE_FLAGS & CLOD_COMPACTION_READ_FLAG_SKIP_REYES_OWNED) != 0u &&
            CLodIsVisibleClusterOwnedByReyes(sourceClusterIndex, CLOD_COMPACTION_REYES_OWNERSHIP_BITSET_DESCRIPTOR_INDEX))
        {
            return;
        }

        ByteAddressBuffer visibleClusters = ResourceDescriptorHeap[CLOD_COMPACTION_VISIBLE_CLUSTERS_BUFFER_DESCRIPTOR_INDEX];
        RWByteAddressBuffer compactedClusters = ResourceDescriptorHeap[CLOD_COMPACTION_COMPACTED_VISIBLE_CLUSTERS_DESCRIPTOR_INDEX];
        StructuredBuffer<uint> offsets = ResourceDescriptorHeap[CLOD_COMPACTION_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX];
        RWStructuredBuffer<uint> writeCursor = ResourceDescriptorHeap[CLOD_COMPACTION_RASTER_BUCKETS_WRITE_CURSOR_DESCRIPTOR_INDEX];
        RWStructuredBuffer<uint> sortedToUnsortedMapping = ResourceDescriptorHeap[CLOD_COMPACTION_SORTED_TO_UNSORTED_MAPPING_DESCRIPTOR_INDEX];
        const uint3 packedCluster = CLodLoadVisibleClusterPacked(visibleClusters, sourceClusterIndex);
        uint bucketIndex = GetRasterBucketIndexFromInstance(CLodVisibleClusterInstanceID(packedCluster));

        uint localOffset = 0;
        InterlockedAdd(writeCursor[bucketIndex], 1, localOffset);

        uint dst = baseClusterOffset + offsets[bucketIndex] + localOffset;
        CLodStoreVisibleClusterPackedWordsRW(compactedClusters, dst, packedCluster);
        sortedToUnsortedMapping[dst] = sourceClusterIndex;
    }

    if (linearizedID < numBuckets)
    {
        StructuredBuffer<uint> offsets = ResourceDescriptorHeap[CLOD_COMPACTION_RASTER_BUCKETS_OFFSETS_DESCRIPTOR_INDEX];
        RWStructuredBuffer<RasterizeClustersCommand> outArgs = ResourceDescriptorHeap[CLOD_COMPACTION_RASTER_BUCKETS_INDIRECT_ARGS_DESCRIPTOR_INDEX];

        uint count = histogram[linearizedID];

        RasterizeClustersCommand cmd = (RasterizeClustersCommand)0;
        if (count > 0)
        {
            const uint kMaxDim = 65535u;
            uint dispatchItemCount = count;
            if ((CLOD_COMPACTION_READ_MODE_FLAGS & CLOD_COMPACTION_READ_FLAG_BUILD_SW_DISPATCH) != 0u)
            {
                dispatchItemCount *= SW_RASTER_GROUPS_PER_CLUSTER;
            }

            uint dispatchX = (uint) ceil(sqrt((float) dispatchItemCount));
            if (dispatchX > kMaxDim)
            {
                dispatchX = kMaxDim;
            }
            uint dispatchY = (dispatchItemCount + dispatchX - 1u) / dispatchX;
            if (dispatchY > kMaxDim)
            {
                dispatchY = kMaxDim;
            }

            cmd.baseClusterOffset = baseClusterOffset + offsets[linearizedID]; // base offset
            cmd.xDim = dispatchX;              // xDim for 2D linearization
            cmd.rasterBucketID = linearizedID;   // bucket index

            cmd.dispatchX = dispatchX;
            cmd.dispatchY = dispatchY;
            cmd.dispatchZ = 1;
        }
        outArgs[linearizedID] = cmd;
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodVirtualShadowGatherStatsCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    Texture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_GATHER_STATS_PAGE_TABLE_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> allocationCountBuffer = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_GATHER_STATS_ALLOCATION_COUNT_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodReyesDispatchIndirectCommand> allocationIndirectArgsBuffer = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_GATHER_STATS_ALLOCATION_INDIRECT_ARGS_DESCRIPTOR_INDEX];
    StructuredBuffer<uint4> pageListHeaderBuffer = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_GATHER_STATS_PAGE_LIST_HEADER_DESCRIPTOR_INDEX];
    StructuredBuffer<CLodVirtualShadowClipmapInfo> clipmapInfos = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_GATHER_STATS_CLIPMAP_INFO_DESCRIPTOR_INDEX];
    RWStructuredBuffer<CLodVirtualShadowStats> statsBuffer = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_GATHER_STATS_STATS_DESCRIPTOR_INDEX];
    const bool capturePreAllocateState = CLOD_VIRTUAL_SHADOW_GATHER_STATS_CAPTURE_PRE_ALLOCATE_STATE != 0u;

    if (dispatchThreadId.x == 0u && dispatchThreadId.y == 0u && dispatchThreadId.z == 0u)
    {
        if (capturePreAllocateState)
        {
            statsBuffer[0].activeClipmapCount = min(perFrameBuffer.numShadowCascades, CLOD_VIRTUAL_SHADOW_GATHER_STATS_CLIPMAP_COUNT);
            statsBuffer[0].allocationRequestCount = allocationCountBuffer.Load(0);
            statsBuffer[0].allocationDispatchGroupCount = allocationIndirectArgsBuffer[0].dispatchX;

            const uint4 pageListHeader = pageListHeaderBuffer[0];
            statsBuffer[0].freePhysicalPageCount = pageListHeader.x;
            statsBuffer[0].reusablePhysicalPageCount = pageListHeader.y;

            uint validClipmapCount = 0u;
            [unroll]
            for (uint clipmapIndex = 0u; clipmapIndex < kCLodVirtualShadowClipmapCount; ++clipmapIndex)
            {
                if (CLodVirtualShadowClipmapIsValid(clipmapInfos[clipmapIndex]))
                {
                    validClipmapCount += 1u;
                }
            }
            statsBuffer[0].validClipmapCount = validClipmapCount;
        }
    }

    if (dispatchThreadId.z >= CLOD_VIRTUAL_SHADOW_GATHER_STATS_CLIPMAP_COUNT ||
        dispatchThreadId.x >= CLOD_VIRTUAL_SHADOW_GATHER_STATS_PAGE_TABLE_RESOLUTION ||
        dispatchThreadId.y >= CLOD_VIRTUAL_SHADOW_GATHER_STATS_PAGE_TABLE_RESOLUTION)
    {
        return;
    }

    const uint pageEntry = pageTable.Load(int4(dispatchThreadId, 0));
    if (pageEntry == 0u)
    {
        return;
    }

    if (capturePreAllocateState)
    {
        CLodVirtualShadowStatsIncrementPreAllocateNonZero(statsBuffer, dispatchThreadId.z);
        if ((pageEntry & kCLodVirtualShadowDirtyMask) != 0u)
        {
            CLodVirtualShadowStatsIncrementPreAllocateDirty(statsBuffer, dispatchThreadId.z);
        }
    }
    else
    {
        CLodVirtualShadowStatsIncrementNonZero(statsBuffer, dispatchThreadId.z);
        if ((pageEntry & kCLodVirtualShadowAllocatedMask) != 0u)
        {
            CLodVirtualShadowStatsIncrementAllocated(statsBuffer, dispatchThreadId.z);
        }
        if ((pageEntry & kCLodVirtualShadowDirtyMask) != 0u)
        {
            CLodVirtualShadowStatsIncrementDirty(statsBuffer, dispatchThreadId.z);
        }
    }
}