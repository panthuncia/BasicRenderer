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

    StructuredBuffer<uint> materialOffset = ResourceDescriptorHeap[UintRootConstant0];
    RWStructuredBuffer<uint> materialWriteCursor = ResourceDescriptorHeap[UintRootConstant1];

    RWStructuredBuffer<PixelRef> pixelList = ResourceDescriptorHeap[UintRootConstant2];

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

// 6) Per-material evaluation scaffold (Compute pass per material group).
// Dispatch with x = ceil(count[m] / kGroupSize), push constants: materialId, baseOffset, count
[numthreads(64, 1, 1)]
void EvaluateMaterialGroupCS(uint3 dtid : SV_DispatchThreadID)
{
    uint materialId = UintRootConstant0;
    uint baseOffset = UintRootConstant1;
    uint count = UintRootConstant2;

    StructuredBuffer<PixelRef> pixelList = ResourceDescriptorHeap[UintRootConstant3];

    uint idx = dtid.x;
    if (idx >= count)
        return;

    PixelRef ref = pixelList[baseOffset + idx];

    // TODO
}