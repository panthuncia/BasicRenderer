#include "include/cbuffers.hlsli"
#include "PerPassRootConstants/clodRayTracingSetupRootConstants.h"

struct PackedRayTracingInstanceDesc
{
    float transform[12];
    uint instanceIDAndMask;
    uint contributionAndFlags;
    uint64_t accelerationStructureDeviceAddress;
};

[numthreads(1, 1, 1)]
void CLodRayTracingTlasSetupCSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0)
    {
        return;
    }

    StructuredBuffer<uint64_t> blasAddresses =
        ResourceDescriptorHeap[CLOD_RT_SETUP_BLAS_ADDRESSES_DESCRIPTOR_INDEX];
    RWStructuredBuffer<PackedRayTracingInstanceDesc> tlasInstances =
        ResourceDescriptorHeap[CLOD_RT_SETUP_TLAS_INSTANCES_DESCRIPTOR_INDEX];

    PackedRayTracingInstanceDesc instanceDesc;
    instanceDesc.transform[0] = 1.0f;
    instanceDesc.transform[1] = 0.0f;
    instanceDesc.transform[2] = 0.0f;
    instanceDesc.transform[3] = 0.0f;
    instanceDesc.transform[4] = 0.0f;
    instanceDesc.transform[5] = 1.0f;
    instanceDesc.transform[6] = 0.0f;
    instanceDesc.transform[7] = 0.0f;
    instanceDesc.transform[8] = 0.0f;
    instanceDesc.transform[9] = 0.0f;
    instanceDesc.transform[10] = 1.0f;
    instanceDesc.transform[11] = 0.0f;
    instanceDesc.instanceIDAndMask = (0u & 0x00FFFFFFu) | (0xFFu << 24u);
    instanceDesc.contributionAndFlags = 0u;
    instanceDesc.accelerationStructureDeviceAddress = blasAddresses[0];

    tlasInstances[0] = instanceDesc;
}
