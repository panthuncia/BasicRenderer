#ifndef __CBUFFERS_HLSL__
#define __CBUFFERS_HLSL__

#include "structs.hlsli"

cbuffer PerObject : register(b1) {
    uint perObjectBufferIndex;
};

cbuffer PerMesh : register(b2) {
    uint perMeshBufferIndex;
    uint perMeshInstanceBufferIndex;
};

cbuffer ShadowInfo : register(b3) {
    int currentLightID; // Used for shadow mapping, global light index
    int lightViewIndex; // Used for shadow mapping, index in light type's shadow view matrix array
};

cbuffer Settings : register(b4) {
    bool enableShadows;
    bool enablePunctualLights;
    bool enableGTAO;
}

cbuffer StaticBufferInfo : register(b5) {
    uint perMeshBufferDescriptorIndex;
    uint normalMatrixBufferDescriptorIndex;
    uint preSkinningVertexBufferDescriptorIndex;
    uint postSkinningVertexBufferDescriptorIndex;
    uint meshletBufferDescriptorIndex;
    uint meshletVerticesBufferDescriptorIndex;
    uint meshletTrianglesBufferDescriptorIndex;
    uint perObjectBufferDescriptorIndex;
    uint cameraBufferDescriptorIndex;
    uint perMeshInstanceBufferDescriptorIndex; // Used by skinned meshes for skinning
    uint drawSetCommandBufferDescriptorIndex;
    uint normalsTextureDescriptorIndex;
    uint aoTextureDescriptorIndex;
}

cbuffer variableBufferInfo : register(b6) {
    uint activeDrawSetIndicesBufferDescriptorIndex;
    uint indirectCommandBufferDescriptorIndex;
    uint maxDrawIndex;
}

cbuffer transparencyInfo : register(b7) {
    uint PPLLHeadsDescriptorIndex;
    uint PPLLNodesDescriptorIndex;
    uint PPLLNodesCounterDescriptorIndex;
    uint PPLLNodePoolSize;
}

cbuffer LightClusterInfo : register(b8) {
    uint lightClusterBufferDescriptorIndex;
    uint lightPagesBufferDescriptorIndex;
    uint lightPagesCounterDescriptorIndex;
    uint lightPagesPoolSize;
}

cbuffer MiscRootConstants : register(b9) { // Used for pass-specific one-off constants
    uint UintRootConstant0;
    uint UintRootConstant1;
    uint UintRootConstant2;
    uint UintRootConstant3;
    uint FloatRootConstant0;
    uint FloatRootConstant1;
}

#endif // __CBUFFERS_HLSL__