#ifndef __CBUFFERS_HLSL__
#define __CBUFFERS_HLSL__

#include "structs.hlsli"

// point-clamp at s0
SamplerState g_pointClamp : register(s0);

// linear-clamp at s1
SamplerState g_linearClamp : register(s1);

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
    uint albedoTextureDescriptorIndex;
    uint metallicRoughnessTextureDescriptorIndex;
    uint emissiveTextureDescriptorIndex;
    
    uint activeLightIndicesBufferDescriptorIndex;
    uint lightBufferDescriptorIndex;
    uint pointLightCubemapBufferDescriptorIndex;
    uint spotLightMatrixBufferDescriptorIndex;
    uint directionalLightCascadeBufferDescriptorIndex;
    uint environmentBufferDescriptorIndex;
}

cbuffer variableBufferInfo : register(b6) {
    uint activeDrawSetIndicesBufferDescriptorIndex;
    uint indirectCommandBufferDescriptorIndex;
    uint meshletFrustrumCullingIndirectCommandBufferDescriptorIndex;
    uint meshletCullingBitfieldBufferDescriptorIndex;
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

cbuffer MiscUintRootConstants : register(b9) { // Used for pass-specific one-off constants
    uint UintRootConstant0;
    uint UintRootConstant1;
    uint UintRootConstant2;
    uint UintRootConstant3;
    uint UintRootConstant4;
    uint UintRootConstant5;
}

cbuffer MiscFloatRootConstants : register(b10) { // Used for pass-specific one-off constants
    float FloatRootConstant0;
    float FloatRootConstant1;
}



#endif // __CBUFFERS_HLSL__