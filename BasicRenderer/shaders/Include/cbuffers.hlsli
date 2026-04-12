#ifndef __CBUFFERS_HLSL__
#define __CBUFFERS_HLSL__

#include "structs.hlsli"

// point-clamp at s0
SamplerState g_pointClamp : register(s0);

// linear-clamp at s1
SamplerState g_linearClamp : register(s1);

cbuffer PerObject : register(b0) {
    uint perObjectBufferIndex;
};

cbuffer PerMesh : register(b1) {
    uint perMeshBufferIndex;
    uint perMeshInstanceBufferIndex;
};

cbuffer ShadowInfo : register(b2) {
    int currentLightID; // Used for shadow mapping, global light index
    int lightViewIndex; // Used for shadow mapping, index in light type's shadow view matrix array
};

cbuffer Settings : register(b3) {
    bool enableShadows;
    bool enablePunctualLights;
    bool enableGTAO;
}

cbuffer MiscUintRootConstants : register(b4) { // Used for pass-specific one-off constants
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
    uint UintRootConstant11;
    uint UintRootConstant12;
    uint UintRootConstant13;
    uint UintRootConstant14;
    uint UintRootConstant15;
    uint UintRootConstant16;
    uint UintRootConstant17;
    uint UintRootConstant18;
    uint UintRootConstant19;
    uint UintRootConstant20;
    uint UintRootConstant21;
    uint UintRootConstant22;
    uint UintRootConstant23;
}

cbuffer ResourceDescriptorIndices : register(b5) {
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
    uint ResourceDescriptorIndex16;
    uint ResourceDescriptorIndex17;
    uint ResourceDescriptorIndex18;
    uint ResourceDescriptorIndex19;
    uint ResourceDescriptorIndex20;
    uint ResourceDescriptorIndex21;
    uint ResourceDescriptorIndex22;
    uint ResourceDescriptorIndex23;
    uint ResourceDescriptorIndex24;
    uint ResourceDescriptorIndex25;
    uint ResourceDescriptorIndex26;
    uint ResourceDescriptorIndex27;
    uint ResourceDescriptorIndex28;
    uint ResourceDescriptorIndex29;
    uint ResourceDescriptorIndex30;
};

cbuffer IndirectCommandSignatureRootConstants : register(b6)
{
    uint IndirectCommandSignatureRootConstant0;
    uint IndirectCommandSignatureRootConstant1;
    uint IndirectCommandSignatureRootConstant2;
    uint IndirectCommandSignatureRootConstant3;
};


#endif // __CBUFFERS_HLSL__