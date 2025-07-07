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

cbuffer variableBufferInfo : register(b5) {
    uint maxDrawIndex;
}

cbuffer transparencyInfo : register(b6) {
    uint PPLLNodePoolSize;
}

cbuffer LightClusterInfo : register(b7) {
    uint lightPagesPoolSize;
}

cbuffer MiscUintRootConstants : register(b8) { // Used for pass-specific one-off constants
    uint UintRootConstant0;
    uint UintRootConstant1;
    uint UintRootConstant2;
    uint UintRootConstant3;
    uint UintRootConstant4;
    uint UintRootConstant5;
    uint UintRootConstant6;
    uint UintRootConstant7;
    uint UintRootConstant8;
    uint UintRootConstant9;
    uint UintRootConstant10;
}

cbuffer MiscFloatRootConstants : register(b9) { // Used for pass-specific one-off constants
    float FloatRootConstant0;
    float FloatRootConstant1;
}

cbuffer ResourceDescriptorIndices : register(b10) {
    uint ResourceDescriptorIndex0;
    uint ResourceDescriptorIndex1;
    uint ResourceDescriptorIndex2;
    uint ResourceDescriptorIndex3;
    uint ResourceDescriptorIndex4;
    uint ResourceDescriptorIndex5;
    uint ResourceDescriptorIndex6;
    uint ResourceDescriptorIndex7;
    uint ResourceDescriptorIndex8;
    uint ResourceDescriptorIndex9;
    uint ResourceDescriptorIndex10;
    uint ResourceDescriptorIndex11;
    uint ResourceDescriptorIndex12;
    uint ResourceDescriptorIndex13;
    uint ResourceDescriptorIndex14;
    uint ResourceDescriptorIndex15;
};



#endif // __CBUFFERS_HLSL__