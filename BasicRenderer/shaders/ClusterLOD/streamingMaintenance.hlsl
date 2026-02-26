#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "Include/clodStructs.hlsli"

static const uint CLOD_STREAM_REQUEST_CAPACITY = (1u << 16);

uint CLodBitMask(uint key)
{
    return 1u << (key & 31u);
}

uint CLodBitWordAddress(uint key)
{
    return (key >> 5u) * 4u;
}

bool CLodReadBit(ByteAddressBuffer bits, uint key)
{
    const uint packed = bits.Load(CLodBitWordAddress(key));
    return (packed & CLodBitMask(key)) != 0u;
}

bool CLodTrySetBit(RWByteAddressBuffer bits, uint key)
{
    uint oldPacked = 0;
    bits.InterlockedOr(CLodBitWordAddress(key), CLodBitMask(key), oldPacked);
    return (oldPacked & CLodBitMask(key)) == 0u;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];

    StructuredBuffer<CLodStreamingRuntimeState> runtimeState =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingRuntimeState)];
    ByteAddressBuffer activeGroupsBits =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingActiveGroupsBits)];
    StructuredBuffer<uint> lastUsedFrames =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingLastUsedFrames)];
    ByteAddressBuffer nonResidentBits =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingNonResidentBits)];

    RWByteAddressBuffer unloadRequestBits =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingUnloadRequestBits)];
    RWStructuredBuffer<CLodStreamingRequest> unloadRequests =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingUnloadRequests)];
    RWStructuredBuffer<uint> unloadRequestCounter =
        ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CLod::StreamingUnloadCounter)];

    const uint groupIndex = dispatchThreadId.x;
    const uint activeScanCount = runtimeState[0].activeGroupScanCount;

    if (groupIndex >= activeScanCount)
    {
        return;
    }

    if (!CLodReadBit(activeGroupsBits, groupIndex))
    {
        return;
    }

    if (CLodReadBit(nonResidentBits, groupIndex))
    {
        return;
    }

    const uint lastUsed = lastUsedFrames[groupIndex];
    if (lastUsed == 0)
    {
        return;
    }

    const uint kUnloadAfterFrames = max(runtimeState[0].unloadAfterFrames, 1u);
    const uint idleFrames = (perFrameBuffer.frameIndex >= lastUsed) ? (perFrameBuffer.frameIndex - lastUsed) : 0u;
    if (idleFrames <= kUnloadAfterFrames)
    {
        return;
    }

    if (!CLodTrySetBit(unloadRequestBits, groupIndex))
    {
        return;
    }

    uint requestIndex = 0;
    InterlockedAdd(unloadRequestCounter[0], 1u, requestIndex);
    if (requestIndex < CLOD_STREAM_REQUEST_CAPACITY)
    {
        CLodStreamingRequest req = (CLodStreamingRequest)0;
        req.groupGlobalIndex = groupIndex;
        unloadRequests[requestIndex] = req;
    }
}
