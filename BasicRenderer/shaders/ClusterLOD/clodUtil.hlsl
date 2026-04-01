#include "include/cbuffers.hlsli"
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
#include "PerPassRootConstants/clodVirtualShadowClearRootConstants.h"
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

static const uint kCLodVirtualShadowAllocatedMask = 0x80000000u;
static const uint kCLodVirtualShadowDirtyMask = 0x40000000u;
static const uint kCLodVirtualShadowPhysicalPageIndexMask = 0x3FFFFFFFu;
static const uint kCLodVirtualShadowClipmapValidFlag = 0x1u;

struct CLodVirtualShadowClipmapInfoGpu
{
    float worldOriginX;
    float worldOriginY;
    float worldOriginZ;
    float texelWorldSize;
    uint pageOffsetX;
    uint pageOffsetY;
    uint pageTableLayer;
    uint shadowCameraBufferIndex;
    uint flags;
    uint pad0;
    uint pad1;
    uint pad2;
};

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
void CLodVirtualShadowClearPhysicalPagesCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= CLOD_VIRTUAL_SHADOW_CLEAR_WIDTH ||
        dispatchThreadId.y >= CLOD_VIRTUAL_SHADOW_CLEAR_HEIGHT)
    {
        return;
    }

    RWTexture2D<uint> physicalPages = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_CLEAR_PHYSICAL_PAGES_DESCRIPTOR_INDEX];
    physicalPages[dispatchThreadId.xy] = 0xFFFFFFFFu;
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

    pageTable[dispatchThreadId] = 0u;

    if (dispatchThreadId.z != 0u)
    {
        return;
    }

    const uint linearIndex = dispatchThreadId.y * CLOD_VIRTUAL_SHADOW_SETUP_PAGE_TABLE_RESOLUTION + dispatchThreadId.x;
    if (linearIndex < CLOD_VIRTUAL_SHADOW_SETUP_PHYSICAL_PAGE_COUNT)
    {
        pageMetadata[linearIndex] = uint4(0u, 0u, 0u, 0u);
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
    StructuredBuffer<CLodVirtualShadowClipmapInfoGpu> clipmapInfos = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_CLIPMAP_INFO_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint4> allocationRequests = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_REQUESTS_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> allocationCountBuffer = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_REQUEST_COUNT_DESCRIPTOR_INDEX];
    RWTexture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_MARK_PAGE_TABLE_DESCRIPTOR_INDEX];

    const uint2 pixel = dispatchThreadId.xy;
    const float linearDepth = depthTexture[pixel];
    if (asuint(linearDepth) == 0x7F7FFFFF)
    {
        return;
    }

    const float2 uv = (float2(pixel) + 0.5f) / float2(CLOD_VIRTUAL_SHADOW_MARK_SCREEN_WIDTH, CLOD_VIRTUAL_SHADOW_MARK_SCREEN_HEIGHT);
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);

    const Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    const float4 clipPos = float4(ndc, 1.0f, 1.0f);
    const float4 viewPosH = mul(clipPos, mainCamera.projectionInverse);
    const float3 positionVS = viewPosH.xyz * linearDepth;
    const float3 positionWS = mul(float4(positionVS, 1.0f), mainCamera.viewInverse).xyz;

    [loop]
    for (uint clipmapIndex = 0u; clipmapIndex < CLOD_VIRTUAL_SHADOW_MARK_CLIPMAP_COUNT; ++clipmapIndex)
    {
        const CLodVirtualShadowClipmapInfoGpu clipmapInfo = clipmapInfos[clipmapIndex];
        if ((clipmapInfo.flags & kCLodVirtualShadowClipmapValidFlag) == 0u ||
            clipmapInfo.shadowCameraBufferIndex == 0xFFFFFFFFu)
        {
            continue;
        }

        const Camera shadowCamera = cameras[clipmapInfo.shadowCameraBufferIndex];
        const float4 shadowClip = mul(float4(positionWS, 1.0f), shadowCamera.viewProjection);
        const float safeW = max(abs(shadowClip.w), 1.0e-6f);
        float3 shadowUv = shadowClip.xyz / safeW;
        shadowUv.xy = shadowUv.xy * 0.5f + 0.5f;
        shadowUv.y = 1.0f - shadowUv.y;

        if (shadowUv.x < 0.0f || shadowUv.x > 1.0f ||
            shadowUv.y < 0.0f || shadowUv.y > 1.0f ||
            shadowUv.z < 0.0f || shadowUv.z > 1.0f)
        {
            continue;
        }

        const uint pageX = min((uint)(shadowUv.x * CLOD_VIRTUAL_SHADOW_MARK_PAGE_TABLE_RESOLUTION), CLOD_VIRTUAL_SHADOW_MARK_PAGE_TABLE_RESOLUTION - 1u);
        const uint pageY = min((uint)(shadowUv.y * CLOD_VIRTUAL_SHADOW_MARK_PAGE_TABLE_RESOLUTION), CLOD_VIRTUAL_SHADOW_MARK_PAGE_TABLE_RESOLUTION - 1u);
        const uint3 pageCoords = uint3(pageX, pageY, clipmapInfo.pageTableLayer);

        uint previousPageState = 0u;
        InterlockedOr(pageTable[pageCoords], kCLodVirtualShadowDirtyMask, previousPageState);
        if ((previousPageState & (kCLodVirtualShadowAllocatedMask | kCLodVirtualShadowDirtyMask)) != 0u)
        {
            continue;
        }

        uint requestIndex = 0u;
        InterlockedAdd(allocationCountBuffer[0], 1u, requestIndex);
        if (requestIndex >= CLOD_VIRTUAL_SHADOW_MARK_MAX_REQUEST_COUNT)
        {
            continue;
        }

        const uint virtualAddress = pageY * CLOD_VIRTUAL_SHADOW_MARK_PAGE_TABLE_RESOLUTION + pageX;
        allocationRequests[requestIndex] = uint4(
            virtualAddress,
            clipmapIndex,
            CLOD_VIRTUAL_SHADOW_MARK_CLIPMAP_COUNT - clipmapIndex,
            0u);
    }

}

[shader("compute")]
[numthreads(64, 1, 1)]
void CLodVirtualShadowAllocatePagesCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    StructuredBuffer<uint4> allocationRequests = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_REQUESTS_DESCRIPTOR_INDEX];
    StructuredBuffer<uint> allocationCountBuffer = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_REQUEST_COUNT_DESCRIPTOR_INDEX];
    RWTexture2DArray<uint> pageTable = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_TABLE_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint4> pageMetadata = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_METADATA_DESCRIPTOR_INDEX];
    RWStructuredBuffer<uint> dirtyFlags = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_ALLOCATE_DIRTY_FLAGS_DESCRIPTOR_INDEX];

    const uint requestIndex = dispatchThreadId.x;
    const uint requestCount = allocationCountBuffer.Load(0);
    if (requestIndex >= requestCount || requestIndex >= CLOD_VIRTUAL_SHADOW_ALLOCATE_PHYSICAL_PAGE_COUNT)
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
    if (pageY >= CLOD_VIRTUAL_SHADOW_ALLOCATE_PAGE_TABLE_RESOLUTION)
    {
        return;
    }

    const uint pageEntry = (requestIndex & kCLodVirtualShadowPhysicalPageIndexMask) | kCLodVirtualShadowAllocatedMask | kCLodVirtualShadowDirtyMask;
    pageTable[uint3(pageX, pageY, clipmapIndex)] = pageEntry;
    pageMetadata[requestIndex] = uint4(virtualAddress, clipmapIndex, request.z, request.w);
    InterlockedOr(dirtyFlags[requestIndex >> 5u], 1u << (requestIndex & 31u));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CLodVirtualShadowBuildDirtyHierarchyCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.z >= CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_CLIPMAP_COUNT)
    {
        return;
    }

    const uint dstResolution = max(CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_RESOLUTION >> 1u, 1u);
    if (dispatchThreadId.x >= dstResolution || dispatchThreadId.y >= dstResolution)
    {
        return;
    }

    Texture2DArray<uint> sourceTexture = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_DESCRIPTOR_INDEX];
    RWTexture2DArray<uint> destTexture = ResourceDescriptorHeap[CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_DEST_DESCRIPTOR_INDEX];

    const uint2 srcBase = dispatchThreadId.xy * 2u;
    uint dirtyValue = 0u;

    [unroll]
    for (uint offsetY = 0u; offsetY < 2u; ++offsetY)
    {
        [unroll]
        for (uint offsetX = 0u; offsetX < 2u; ++offsetX)
        {
            const uint srcX = min(srcBase.x + offsetX, CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_RESOLUTION - 1u);
            const uint srcY = min(srcBase.y + offsetY, CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_RESOLUTION - 1u);
            const uint srcValue = sourceTexture.Load(int4(srcX, srcY, dispatchThreadId.z, 0));
            const uint isDirty = (CLOD_VIRTUAL_SHADOW_DIRTY_HIERARCHY_SOURCE_IS_PAGE_TABLE != 0u)
                ? ((srcValue & kCLodVirtualShadowDirtyMask) != 0u ? 1u : 0u)
                : (srcValue != 0u ? 1u : 0u);
            dirtyValue = max(dirtyValue, isDirty);
        }
    }

    destTexture[dispatchThreadId] = dirtyValue;
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