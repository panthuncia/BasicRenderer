#ifndef __STRUCTS_HLSL__
#define __STRUCTS_HLSL__

struct PerFrameBuffer {
    row_major matrix view;
    row_major matrix projection;
    float4 eyePosWorldSpace;
    float4 ambientLighting;
    float4 shadowCascadeSplits;
    uint lightBufferIndex;
    uint numLights;
    uint pointLightCubemapBufferIndex;
    uint spotLightMatrixBufferIndex;
    uint directionalLightCascadeBufferIndex;
    uint numShadowCascades;
    uint environmentIrradianceMapIndex;
    uint environmentIrradianceSamplerIndex;
    uint environmentPrefilteredMapIndex;
    uint environmentPrefilteredSamplerIndex;
    uint environmentBRDFLUTIndex;
    uint environmentBRDFLUTSamplerIndex;
    uint outputType;
    uint perObjectBufferIndex;
};

struct LightInfo {
    uint type;
    float innerConeAngle;
    float outerConeAngle;
    int shadowViewInfoIndex; // -1 if no shadow map
    float4 posWorldSpace; // Position of the light
    float4 dirWorldSpace; // Direction of the light
    float4 attenuation; // x,y,z = constant, linear, quadratic attenuation, w= max range
    float4 color; // Color of the light
    float nearPlane;
    float farPlane;
    int shadowMapIndex;
    int shadowSamplerIndex;
};

struct MaterialInfo {
    uint materialFlags;
    uint baseColorTextureIndex;
    uint baseColorSamplerIndex;
    uint normalTextureIndex;
    uint normalSamplerIndex;
    uint metallicRoughnessTextureIndex;
    uint metallicRoughnessSamplerIndex;
    uint emissiveTextureIndex;
    uint emissiveSamplerIndex;
    uint aoMapIndex;
    uint aoSamplerIndex;
    uint heightMapIndex;
    uint heightSamplerIndex;
    float metallicFactor;
    float roughnessFactor;
    float ambientStrength;
    float specularStrength;
    float textureScale;
    float heightMapScale;
    float pad0;
    float4 baseColorFactor;
    float4 emissiveFactor;
};

struct SingleMatrix {
    row_major matrix value;
};

struct Meshlet {
    uint VertOffset;
    uint TriOffset;
    uint VertCount;
    uint TriCount;
};

struct PerObjectBuffer {
    row_major matrix model;
    row_major float4x4 normalMatrix;
    uint boneTransformBufferIndex;
    uint inverseBindMatricesBufferIndex;
    uint isValid;
    uint pad0;
};

#endif // __STRUCTS_HLSL__