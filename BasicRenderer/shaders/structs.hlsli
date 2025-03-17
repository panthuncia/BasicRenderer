#ifndef __STRUCTS_HLSL__
#define __STRUCTS_HLSL__

struct ClippingPlane {
    float4 plane;
};

struct Camera {
    float4 positionWorldSpace;
    row_major matrix view;
    row_major matrix projection;
    row_major matrix viewProjection;
    ClippingPlane clippingPlanes[6];
};

struct PerFrameBuffer {
    float4 ambientLighting;
    float4 shadowCascadeSplits;
    uint mainCameraIndex;
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
    uint pad[2];
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
    uint metallicTextureIndex;
    uint metallicSamplerIndex;
    uint roughnessTextureIndex;
    uint roughnessSamplerIndex;
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
    float alphaCutoff;
    uint pad0;
    uint pad1;
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
    uint normalMatrixBufferIndex;
    uint pad[3];
};

struct BoundingSphere {
    float4 center;
    float radius;
};

struct PerMeshBuffer {
    uint materialDataIndex;
    uint vertexFlags;
    uint vertexByteSize;
    uint skinningVertexByteSize;
    uint vertexBufferOffset;
    uint meshletBufferOffset;
    uint meshletVerticesBufferOffset;
    uint meshletTrianglesBufferOffset;
    BoundingSphere boundingSphere;
    uint numVertices;
    uint pad[2];
};

struct PerMeshInstanceBuffer {
    uint boneTransformBufferIndex;
    uint inverseBindMatricesBufferIndex;
    uint preSkinningVertexBufferOffset;
    uint pad[1];
};

#endif // __STRUCTS_HLSL__