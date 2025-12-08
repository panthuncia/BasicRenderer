#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/utilities.hlsli"
#include "Include/meshletCommon.hlsli"

struct PixelRef
{
    uint pixelXY; // 32 bits: x = low 16, y = high 16
};

uint DecodeClusterIndex(uint packed)
{
    return packed & 0x1FFFFFFu; /* 25 bits */
}
uint DecodePrimID(uint packed)
{
    return packed >> 25;
}

uint GetMaterialIdFromCluster(uint clusterIndex,
                              StructuredBuffer<VisibleClusterInfo> visibleClusterTable,
                              StructuredBuffer<PerMeshInstanceBuffer> perMeshInstance,
                              StructuredBuffer<PerMeshBuffer> perMeshBuffer)
{
    VisibleClusterInfo clusterData = visibleClusterTable[clusterIndex];
    uint perMeshInstanceBufferIndex = clusterData.drawcallIndexAndMeshletIndex.x;
    PerMeshInstanceBuffer instanceData = perMeshInstance[perMeshInstanceBufferIndex];
    PerMeshBuffer meshBuffer = perMeshBuffer[instanceData.perMeshBufferIndex];

    ConstantBuffer<MaterialInfo> materialInfo = ResourceDescriptorHeap[meshBuffer.materialDataIndex];

    return materialInfo.compileFlagsID;
}

// 2) Clear counters – can be ClearUAV or compute. Compute version:
// UintRootConstant0 = NumMaterials
[numthreads(64, 1, 1)]
void ClearMaterialCountersCS(uint3 tid : SV_DispatchThreadID)
{
    RWStructuredBuffer<uint> materialPixelCount = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisUtil::MaterialPixelCountBuffer)];

    uint numMaterials = UintRootConstant0;
    uint i = tid.x;
    if (i < numMaterials)
    {
        materialPixelCount[i] = 0;
    }
}

// 3) Histogram: one thread per pixel, atomic into count[m].
// TODO: optimize with shared-memory tiling?
[numthreads(8, 8, 1)]
void MaterialHistogramCS(uint3 dtid : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrame = ResourceDescriptorHeap[0];
    uint screenW = perFrame.screenResX;
    uint screenH = perFrame.screenResY;
    if (dtid.x >= screenW || dtid.y >= screenH)
        return;

    Texture2D<uint2> visibility = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibilityTexture)];
    StructuredBuffer<VisibleClusterInfo> visibleClusterTable = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibleClusterTable)];
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstance = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];

    RWStructuredBuffer<uint> materialPixelCount = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisUtil::MaterialPixelCountBuffer)];

    uint2 pixel = dtid.xy;
    uint2 vis = visibility[pixel];
    uint packed = vis.x;
    if (packed == 0xFFFFFFFF) {
        return; // no visible geometry
    }

    uint clusterIndex = DecodeClusterIndex(packed);
    uint primId = DecodePrimID(packed);

    // Derive material ID
    uint matId = GetMaterialIdFromCluster(clusterIndex, visibleClusterTable, perMeshInstance, perMeshBuffer);

    InterlockedAdd(materialPixelCount[matId], 1);
}

// 5) Build grouped pixel list: use offsets[] as base and a per-material write cursor (atomic++).
[numthreads(8, 8, 1)]
void BuildPixelListCS(uint3 dtid : SV_DispatchThreadID)
{
    ConstantBuffer<PerFrameBuffer> perFrame = ResourceDescriptorHeap[0];
    uint screenW = perFrame.screenResX;
    uint screenH = perFrame.screenResY;
    if (dtid.x >= screenW || dtid.y >= screenH)
        return;

    Texture2D<uint2> visibility = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibilityTexture)];
    StructuredBuffer<VisibleClusterInfo> visibleClusterTable = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::VisibleClusterTable)];
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstance = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];

    StructuredBuffer<uint> materialOffset = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisUtil::MaterialOffsetBuffer)];
    RWStructuredBuffer<uint> materialWriteCursor = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisUtil::MaterialWriteCursorBuffer)];

    RWStructuredBuffer<PixelRef> pixelList = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisUtil::PixelListBuffer)];

    uint2 pixel = dtid.xy;
    uint2 vis = visibility[pixel];
    uint packed = vis.x;
    if (packed == 0)
        return;

    uint clusterIndex = DecodeClusterIndex(packed);
    uint primId = DecodePrimID(packed);
    uint matId = GetMaterialIdFromCluster(clusterIndex, visibleClusterTable, perMeshInstance, perMeshBuffer);

    uint base = materialOffset[matId];
    uint localIndex;
    InterlockedAdd(materialWriteCursor[matId], 1, localIndex);

    uint dst = base + localIndex;
    PixelRef ref;
    ref.pixelXY = (pixel.x & 0xFFFFu) | ((pixel.y & 0xFFFFu) << 16); // pack xy
    pixelList[dst] = ref;
}

// Indirect command record layout: 4 root constants + 3 dispatch args.
// This must match the command signature (Constant, Constant, Constant, Constant, Dispatch).
struct MaterialEvaluationIndirectArgs {
    // Root constants (all uints):
    uint materialId; // UintRootConstant0
    uint baseOffset; // UintRootConstant1
    uint count; // UintRootConstant2

    // Dispatch arguments:
    uint dispatchX; // D3D12_DISPATCH_ARGUMENTS.x
    uint dispatchY; // D3D12_DISPATCH_ARGUMENTS.y
    uint dispatchZ; // D3D12_DISPATCH_ARGUMENTS.z
};

#define MATERIAL_EXECUTION_GROUP_SIZE 64u

// Build per-material indirect compute args.
// Inputs:
//  - counts[m] = number of pixels for material m
//  - offsets[m] = base offset into PixelListBuffer where this material’s pixels start
// Root constants:
//  - UintRootConstant0 = NumMaterials
//  - UintRootConstant1 = PixelList SRV descriptor index
// Output:
//  - one ComputeIndirectArgs per material, written at index=materialId
[numthreads(64, 1, 1)]
void BuildEvaluateIndirectArgsCS(uint3 dtid : SV_DispatchThreadID)
{
    uint numMaterials = UintRootConstant0;
    if (dtid.x >= numMaterials)
        return;

    StructuredBuffer<uint> counts = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisUtil::MaterialPixelCountBuffer)];
    StructuredBuffer<uint> offsets = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisUtil::MaterialOffsetBuffer)];
    RWStructuredBuffer<MaterialEvaluationIndirectArgs> outArgs = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::IndirectCommandBuffers::MaterialEvaluationCommandBuffer)];

    uint materialId = dtid.x;
    uint count = counts[materialId];

    // If material has no pixels, write a zero-dispatch
    if (count == 0)
    {
        MaterialEvaluationIndirectArgs zero = (MaterialEvaluationIndirectArgs) 0;
        outArgs[materialId] = zero;
        return;
    }

    uint baseOffset = offsets[materialId];
    uint pixelListSrvIndex = UintRootConstant1;

    // We index the pixel list linearly; pick 2D dispatch that minimizes wasted threads.

    // Number of thread groups required overall (linear space):
    uint groupsNeeded = (count + MATERIAL_EXECUTION_GROUP_SIZE - 1u) / MATERIAL_EXECUTION_GROUP_SIZE;

    // Pick 2D dispatch that fits DX12 limits and keeps near-square shape
    const uint kMaxDim = 65535u;
    // Start from sqrt(groupsNeeded) for near-square tiling
    uint dispatchX = (uint) ceil(sqrt((float) groupsNeeded));
    if (dispatchX > kMaxDim)
        dispatchX = kMaxDim;
    uint dispatchY = (groupsNeeded + dispatchX - 1u) / dispatchX;
    if (dispatchY > kMaxDim)
    {
        dispatchY = kMaxDim; // With screen sizes in practice, this won't clamp
    }
    
    MaterialEvaluationIndirectArgs args;
    args.materialId = materialId;
    args.baseOffset = baseOffset;
    args.count = count;

    args.dispatchX = dispatchX;
    args.dispatchY = dispatchY;
    args.dispatchZ = 1;

    outArgs[materialId] = args;
}

// Root constants (via ExecuteIndirect / command signature):
//   UintRootConstant0 = materialId
//   UintRootConstant1 = baseOffset into PixelListBuffer
//   UintRootConstant2 = count (number of pixels for this material)
//
// Thread layout must match what the command builder assumed (64 here).
[numthreads(64, 1, 1)]
void EvaluateMaterialGroupBasicCS(
    uint3 dispatchThreadId : SV_DispatchThreadID
)
{
    uint materialId = UintRootConstant0;
    uint baseOffset = UintRootConstant1;
    uint count = UintRootConstant2;

    uint idx = dispatchThreadId.x;
    if (idx >= count)
        return;

    StructuredBuffer<PixelRef> pixelList = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::VisUtil::PixelListBuffer)];

    // Look up this thread's pixel
    PixelRef ref = pixelList[baseOffset + idx];

    // Unpack to xy
    uint2 pixel;
    pixel.x = ref.pixelXY & 0xFFFFu;
    pixel.y = ref.pixelXY >> 16;

    // TODO
}