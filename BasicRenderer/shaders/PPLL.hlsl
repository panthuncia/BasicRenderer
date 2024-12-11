#include "vertex.hlsli"
#include "cbuffers.hlsli"
#include "lighting.hlsli"
#include "tonemapping.hlsli"
#include "outputTypes.hlsli"
//https://github.com/GPUOpen-Effects/TressFX/blob/master/src/Shaders/TressFXPPLL.hlsl

#define FRAGMENT_LIST_NULL 0xffffffff
#define K_NEAREST 8 // must be a power of 2
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
    
    float4 finalOutput;
    switch (perFrameBuffer.outputType) {
        case OUTPUT_COLOR:
            finalOutput = float4(lightingOutput.lighting.xyz, lightingOutput.baseColor.a);
            break;
        case OUTPUT_NORMAL: // Normal
            finalOutput = float4(lightingOutput.normalWS * 0.5 + 0.5, lightingOutput.baseColor.a);
            break;
        case OUTPUT_ALBEDO:
            finalOutput = float4(lightingOutput.baseColor.rgb, lightingOutput.baseColor.a);
            break;
        case OUTPUT_METALLIC:
            finalOutput = float4(lightingOutput.metallic, lightingOutput.metallic, lightingOutput.metallic, lightingOutput.baseColor.a);
            break;
        case OUTPUT_ROUGHNESS:
            finalOutput = float4(lightingOutput.roughness, lightingOutput.roughness, lightingOutput.roughness, lightingOutput.baseColor.a);
            break;
        case OUTPUT_EMISSIVE:{
                if (materialInfo.materialFlags & MATERIAL_EMISSIVE_TEXTURE) {
                    float3 srgbEmissive = LinearToSRGB(lightingOutput.emissive);
                    finalOutput = float4(srgbEmissive, lightingOutput.baseColor.a);
                }
                else {
                    finalOutput = float4(materialInfo.emissiveFactor.rgb, lightingOutput.baseColor.a);
                }
                break;
            }
        case OUTPUT_AO:
            finalOutput = float4(lightingOutput.ao, lightingOutput.ao, lightingOutput.ao, lightingOutput.baseColor.a);
            break;
        case OUTPUT_DEPTH:{
                float depth = abs(input.positionViewSpace.z) * 0.1;
                finalOutput = float4(depth, depth, depth, lightingOutput.baseColor.a);
                break;
            }
#if defined(PSO_IMAGE_BASED_LIGHTING)
        case OUTPUT_METAL_BRDF_IBL:
            finalOutput = float4(lightingOutput.f_metal_brdf_ibl, lightingOutput.baseColor.a);
            break;
        case OUTPUT_DIELECTRIC_BRDF_IBL:
            finalOutput = float4(lightingOutput.f_dielectric_brdf_ibl, lightingOutput.baseColor.a);
            break;
        case OUTPUT_SPECULAR_IBL:
            finalOutput = float4(lightingOutput.f_specular_metal, lightingOutput.baseColor.a);
            break;
        case OUTPUT_METAL_FRESNEL_IBL:
            finalOutput = float4(lightingOutput.f_metal_fresnel_ibl, lightingOutput.baseColor.a);
            break;
        case OUTPUT_DIELECTRIC_FRESNEL_IBL:
            finalOutput = float4(lightingOutput.f_dielectric_fresnel_ibl, lightingOutput.baseColor.a);
            break;
#endif // IMAGE_BASED_LIGHTING
        case OUTPUT_MESHLETS:{
                finalOutput = lightMeshlets(input.meshletIndex, lightingOutput.normalWS, lightingOutput.viewDir);
                break;
            }
        default:
            finalOutput = float4(1.0, 0.0, 0.0, 1.0);
            break;
    }
    
    WriteFragmentAttributes(nNewFragmentAddress, nOldFragmentAddress, float4(finalOutput.rgb, finalOutput.a), input.position.z, LinkedListUAV);
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
    float depth;
    float4 color;
};


void SortNearest(inout KBUFFER_STRUCT fragments[K_NEAREST]) {
    // Bitonic sort- requires power of 2 kbuffer
    [loop]
    for (uint k = 2; k <= K_NEAREST; k <<= 1) {
        [loop]
        for (uint j = k >> 1; j > 0; j >>= 1) {
            [loop]
            for (uint i = 0; i < K_NEAREST; i++) {
                uint ixj = i ^ j;
                if (ixj > i && ixj < K_NEAREST) {
                    // Compare and swap based on depth
                    if (fragments[i].depth > fragments[ixj].depth) {
                        KBUFFER_STRUCT temp = fragments[i];
                        fragments[i] = fragments[ixj];
                        fragments[ixj] = temp;
                    }
                }
            }
        }
    }
}

float4 PPLLResolvePS(VS_OUTPUT input) : SV_Target {
    Texture2D<uint> RWFragmentListHead = ResourceDescriptorHeap[PPLLHeadsDescriptorIndex];
    StructuredBuffer<PPLL_STRUCT> LinkedListUAV = ResourceDescriptorHeap[PPLLNodesDescriptorIndex];

    uint2 pixelCoord = (uint2) input.position.xy;

    // Get the head of the fragment list for this pixel
    uint head = RWFragmentListHead[pixelCoord];
    if (head == FRAGMENT_LIST_NULL) {
        // No fragments for this pixel
        return float4(0, 0, 0, 0);
    }

    uint fragmentIndices[MAX_FRAGMENTS];
    uint count = 0;

    // Gather all fragments
    uint current = head;
    while (current != FRAGMENT_LIST_NULL && count < MAX_FRAGMENTS) {
        fragmentIndices[count++] = current;
        PPLL_STRUCT node = LinkedListUAV[current];
        current = node.uNext;
    }

    KBUFFER_STRUCT nearestFragments[K_NEAREST];
    uint nearestCount = 0;

    uint otherFragments[MAX_FRAGMENTS - K_NEAREST];
    uint otherCount = 0;

    // Separate the fragments into "nearest" and "others"
    for (uint i = 0; i < count; i++) {
        uint fragIndex = fragmentIndices[i];
        float depth = LinkedListUAV[fragIndex].depth;

        if (nearestCount < K_NEAREST) {
            // We still have room in the nearestFragments array
            nearestFragments[nearestCount].depth = depth;
            nearestFragments[nearestCount].color = LinkedListUAV[fragIndex].color;
            nearestCount++;
        }
        else {
            // Find the farthest fragment in nearestFragments
            float farthestDepth = 0;
            uint farthestIndex = 0;
            for (uint fi = 0; fi < nearestCount; fi++) {
                float fd = nearestFragments[fi].depth;
                if (fd > farthestDepth) {
                    farthestDepth = fd;
                    farthestIndex = fi;
                }
            }

            if (depth < farthestDepth) {
                // Replace the farthest one
                nearestFragments[farthestIndex].depth = depth;
                nearestFragments[farthestIndex].color = LinkedListUAV[fragIndex].color;
            }
            else {
                // Put it into otherFragments for later blending
                otherFragments[otherCount++] = fragIndex;
            }
        }
    }
        
    // Fill unused entries in nearestFragments with dummy values
    // so the entire array has K_NEAREST entries.
    for (uint i = nearestCount; i < K_NEAREST; i++) {
        // Large depth so these dummy entries sort to the back.
        nearestFragments[i].depth = 0xFFFFFFFF;
        nearestFragments[i].color = float4(0, 0, 0, 0);
    }

    SortNearest(nearestFragments);

    // Blend nearestFragments first (front-to-back)
    float4 outColor = float4(0, 0, 0, 0);
    for (uint i = 0; i < K_NEAREST; i++) {
        // Skip dummy fragments (if depth == 0xFFFFFFFF, it's dummy)
        if (nearestFragments[i].depth == 0xFFFFFFFF)
            break;
        float4 srcColor = nearestFragments[i].color;
        // "over" operation: outColor = outColor + srcColor * (1.0f - outColor.a)
        outColor = outColor + srcColor * (1.0f - outColor.a);
    }

    // Blend the remaining fragments without sorting
    for (uint i = 0; i < otherCount; i++) {
        float4 srcColor = LinkedListUAV[otherFragments[i]].color;
        outColor = outColor + srcColor * (1.0f - outColor.a);
    }

    outColor.xyz = reinhardJodie(outColor.xyz);
    outColor.xyz = LinearToSRGB(outColor.xyz);
    return outColor;
}