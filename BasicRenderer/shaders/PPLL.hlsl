#include "vertex.hlsli"
#include "cbuffers.hlsli"
#include "lighting.hlsli"
//https://github.com/GPUOpen-Effects/TressFX/blob/master/src/Shaders/TressFXPPLL.hlsl

#define FRAGMENT_LIST_NULL 0xffffffff
#define KBUFFER_SIZE 8
#define MAX_FRAGMENTS 512

struct PPLL_STRUCT {
    float depth;
    float4 color;
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

// Write fragment attributes to list location. 
void WriteFragmentAttributes(int nAddress, int nPreviousLink, float4 vColor, float fDepth, RWStructuredBuffer<PPLL_STRUCT> LinkedListUAV) {
    PPLL_STRUCT element;
    element.color = vColor; //PackFloat4IntoUint(vColor);
    element.depth = fDepth;
    element.uNext = nPreviousLink;
    LinkedListUAV[nAddress] = element;
}

[earlydepthstencil]
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

//Resolve vertex Shader
struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD1;
};
VS_OUTPUT VSMain(float3 pos : POSITION, float2 uv : TEXCOORD0) {
    VS_OUTPUT output;
    output.position = float4(pos, 1.0f);
    output.uv = uv;
    return output;
}

struct KBUFFER_STRUCT {
    uint depth;
    float4 color;
};

/*float4 GatherLinkedList(float2 vfScreenAddress) {
    uint2 vScreenAddress = uint2(vfScreenAddress);
    RWTexture2D<uint> RWFragmentListHead = ResourceDescriptorHeap[PPLLHeadsDescriptorIndex];
    RWStructuredBuffer<PPLL_STRUCT> LinkedListUAV = ResourceDescriptorHeap[PPLLNodesDescriptorIndex];
    
        // Convert SV_POSITION to pixel coordinates if necessary
    // If the input.position.xy are already pixel coords, just cast.
    uint2 pixelCoord = (uint2)input.position.xy;

    // Get the head of the fragment list for this pixel
    uint head = FragmentListHead[pixelCoord];
    if (head == FRAGMENT_LIST_NULL)
    {
        // No fragments for this pixel
        return float4(0,0,0,0);
    }

    // Gather the fragments
    uint fragmentIndices[MAX_FRAGMENTS];
    uint count = 0;

    uint current = head;
    while (current != FRAGMENT_LIST_NULL && count < MAX_FRAGMENTS)
    {
        fragmentIndices[count++] = current;
        current = PPLLNodes[current].uNext;
    }

    // Sort the fragments by depth (front to back)
    // Front-to-back means smallest depth first
    for (uint i = 0; i < count; i++)
    {
        for (uint j = i + 1; j < count; j++)
        {
            if (PPLLNodes[fragmentIndices[j]].depth < PPLLNodes[fragmentIndices[i]].depth)
            {
                uint temp = fragmentIndices[i];
                fragmentIndices[i] = fragmentIndices[j];
                fragmentIndices[j] = temp;
            }
        }
    }

    // Blend fragments front-to-back using standard alpha compositing
    float4 outColor = float4(0,0,0,0);
    for (uint i = 0; i < count; i++)
    {
        float4 srcColor = PPLLNodes[fragmentIndices[i]].color;
        // "over" operation: outColor = srcColor + (1 - srcColor.a)*outColor
        // To handle this correctly: outColor = outColor + srcColor * (1 - outColor.a)
        outColor = outColor + srcColor * (1.0f - outColor.a);
    }

    return outColor;
}*/

[earlydepthstencil]
float4 PPLLResolvePS(VS_OUTPUT input) : SV_Target {
    Texture2D<uint> RWFragmentListHead = ResourceDescriptorHeap[PPLLHeadsDescriptorIndex];
    StructuredBuffer<PPLL_STRUCT> LinkedListUAV = ResourceDescriptorHeap[PPLLNodesDescriptorIndex];
// Convert SV_POSITION to pixel coordinates if necessary
    // If the input.position.xy are already pixel coords, just cast.
    uint2 pixelCoord = (uint2) input.position.xy;

    // Get the head of the fragment list for this pixel
    uint head = RWFragmentListHead[pixelCoord];
    if (head == FRAGMENT_LIST_NULL) {
        // No fragments for this pixel
        return float4(0, 0, 0, 0);
    }

    // Gather the fragments
    uint fragmentIndices[MAX_FRAGMENTS];
    uint count = 0;

    uint current = head;
    while (current != FRAGMENT_LIST_NULL && count < MAX_FRAGMENTS) {
        fragmentIndices[count++] = current;
        current = LinkedListUAV[current].uNext;
    }

    // Sort the fragments by depth (front to back)
    // Front-to-back means smallest depth first
    for (uint i = 0; i < count; i++) {
        for (uint j = i + 1; j < count; j++) {
            if (LinkedListUAV[fragmentIndices[j]].depth < LinkedListUAV[fragmentIndices[i]].depth) {
                uint temp = fragmentIndices[i];
                fragmentIndices[i] = fragmentIndices[j];
                fragmentIndices[j] = temp;
            }
        }
    }

    // Blend fragments front-to-back using standard alpha compositing
    float4 outColor = float4(0, 0, 0, 0);
    for (uint i = 0; i < count; i++) {
        float4 srcColor = LinkedListUAV[fragmentIndices[i]].color;
        // "over" operation: outColor = srcColor + (1 - srcColor.a)*outColor
        // To handle this correctly: outColor = outColor + srcColor * (1 - outColor.a)
        outColor = outColor + srcColor * (1.0f - outColor.a);
    }

    return outColor;
}