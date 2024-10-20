cbuffer PerObject : register(b1) {
    row_major matrix model;
    row_major float4x4 normalMatrix;
    uint boneTransformBufferIndex;
    uint inverseBindMatricesBufferIndex;
};

cbuffer PerMesh : register(b2) {
    uint materialDataIndex;
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