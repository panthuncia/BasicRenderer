#ifndef __CBUFFERS_HLSL__
#define __CBUFFERS_HLSL__

#include "structs.hlsli"

cbuffer PerObject : register(b1) {
    uint perObjectBufferIndex;
};

cbuffer PerMesh : register(b2) {
    uint perMeshBufferIndex;
};

cbuffer RootConstants1 : register(b3) {
    int currentLightID; // Used for shadow mapping, global light index
};

cbuffer RootConstants2 : register(b4) {
    int lightViewIndex; // Used for shadow mapping, index in light type's shadow view matrix array
}

cbuffer Settings : register(b5) {
    bool enableShadows;
    bool enablePunctualLights;
}

cbuffer StaticBufferIndices : register(b6) {
    uint vertexBufferDescriptorIndex;
    uint meshletBufferDescriptorIndex;
    uint meshletVerticesBufferDescriptorIndex;
    uint meshletTrianglesBufferDescriptorIndex;
    uint perObjectBufferDescriptorIndex;
    uint cameraBufferDescriptorIndex;
}

cbuffer variableBufferIndices : register(b7) {
    uint perMeshBufferDescriptorIndex; // Variable between opaque vs. transparent
    uint drawSetCommandBufferDescriptorIndex;
    uint activeDrawSetIndicesBufferDescriptorIndex;
    uint indirectCommandBufferDescriptorIndex;
    uint maxDrawIndex;
}

cbuffer transparencyBufferIndices : register(b8) {
    uint PPLLHeadsDescriptorIndex;
    uint PPLLNodesDescriptorIndex;
    uint PPLLNodesCounterDescriptorIndex;
    uint PPLLNodePoolSize;
}

#endif // __CBUFFERS_HLSL__