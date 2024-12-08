#include "vertex.hlsli"
#include "cbuffers.hlsli"
#include "lighting.hlsli"
//https://github.com/GPUOpen-Effects/TressFX/blob/master/src/Shaders/TressFXPPLL.hlsl

#define FRAGMENT_LIST_NULL 0xffffffff

struct PPLL_STRUCT {
    uint depth;
    uint color;
    uint uNext;
};

// Allocate a new fragment location in fragment color, depth, and link buffers
int AllocateFragment(int2 vScreenAddress, RWStructuredBuffer<uint> LinkedListCounter) {
    uint newAddress;
    InterlockedAdd(LinkedListCounter[0], 1, newAddress);

    if (newAddress < 0 || newAddress >= PPLLNodePoolSize)
        newAddress = FRAGMENT_LIST_NULL;
    return newAddress;
}

// Insert a new fragment at the head of the list. The old list head becomes the
// the second fragment in the list and so on. Return the address of the *old* head.
int MakeFragmentLink(int2 vScreenAddress, int nNewHeadAddress, RWTexture2D<uint> RWFragmentListHead) {
    int nOldHeadAddress;
    InterlockedExchange(RWFragmentListHead[vScreenAddress], nNewHeadAddress, nOldHeadAddress);
    return nOldHeadAddress;
}

// Pack a float4 into an uint
uint PackFloat4IntoUint(float4 vValue) {
    return (((uint) (vValue.x * 255)) << 24) | (((uint) (vValue.y * 255)) << 16) | (((uint) (vValue.z * 255)) << 8) | (uint) (vValue.w * 255);
}

// Pack a float3 and a uint8 into an uint
uint PackFloat3ByteIntoUint(float3 vValue, uint uByteValue) {
    return (((uint) (vValue.x * 255)) << 24) | (((uint) (vValue.y * 255)) << 16) | (((uint) (vValue.z * 255)) << 8) | uByteValue;
}

// Write fragment attributes to list location. 
void WriteFragmentAttributes(int nAddress, int nPreviousLink, float4 vColor, float fDepth, RWStructuredBuffer<PPLL_STRUCT> LinkedListUAV) {
    PPLL_STRUCT element;
    element.color = PackFloat4IntoUint(vColor);
    element.depth = asuint(saturate(fDepth));
    element.uNext = nPreviousLink;
    LinkedListUAV[nAddress] = element;
}

//[earlydepthstencil]
void PPLLFillPS(PSInput input, bool isFrontFace : SV_IsFrontFace) {
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    uint meshBufferIndex = perMeshBufferIndex;
    PerMeshBuffer meshBuffer = perMeshBuffer[meshBufferIndex];
    ConstantBuffer<MaterialInfo> materialInfo = ResourceDescriptorHeap[meshBuffer.materialDataIndex];

    // Light fragment
    
    LightingOutput lightingOutput = lightFragment(mainCamera, input, materialInfo, meshBuffer, perFrameBuffer, isFrontFace);

    
    // Fill the PPLL buffers with the fragment data
    
    RWTexture2D<uint> RWFragmentListHead = ResourceDescriptorHeap[PPLLHeadsDescriptorIndex];
    RWStructuredBuffer<PPLL_STRUCT> LinkedListUAV = ResourceDescriptorHeap[PPLLNodesDescriptorIndex];
    RWStructuredBuffer<uint> LinkedListCounter = ResourceDescriptorHeap[PPLLNodesCounterDescriptorIndex];
    
    // Screen address
    uint2 vScreenAddress = uint2(input.position.xy);
    
    // Allocate a new fragment
    int nNewFragmentAddress = AllocateFragment(vScreenAddress, LinkedListCounter);
    int nOldFragmentAddress = MakeFragmentLink(vScreenAddress, nNewFragmentAddress, RWFragmentListHead);
    WriteFragmentAttributes(nNewFragmentAddress, nOldFragmentAddress, float4(lightingOutput.lighting.xyz, lightingOutput.baseColor.a), input.position.z, LinkedListUAV);
}