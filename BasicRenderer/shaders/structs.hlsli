#ifndef __STRUCTS_HLSL__
#define __STRUCTS_HLSL__

struct ClippingPlane {
    float4 plane;
};

struct Camera {
    float4 positionWorldSpace;
    row_major matrix view;
    row_major matrix projection;
    row_major matrix projectionInverse;
    row_major matrix viewProjection;
    ClippingPlane clippingPlanes[6];
    float fov;
    float aspectRatio;
    float zNear;
    float zFar;
};

struct PerFrameBuffer {
    float4 ambientLighting;
    float4 shadowCascadeSplits;
    uint mainCameraIndex;
    uint activeLightIndicesBufferIndex;
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
    uint screenResX;
    uint screenResY;
    uint lightClusterGridSizeX;
    uint lightClusterGridSizeY;
    uint lightClusterGridSizeZ;
};

struct BoundingSphere {
    float4 center;
    float radius;
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
    bool shadowCaster;
    BoundingSphere boundingSphere;
    float maxRange;
    uint pad[1];
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

struct PerMeshBuffer {
    uint materialDataIndex;
    uint vertexFlags;
    uint vertexByteSize;
    uint skinningVertexByteSize;
    uint inverseBindMatricesBufferIndex;
    uint vertexBufferOffset;
    uint meshletBufferOffset;
    uint meshletVerticesBufferOffset;
    uint meshletTrianglesBufferOffset;
    BoundingSphere boundingSphere;
    uint numVertices;
    uint pad[1];
};

struct PerMeshInstanceBuffer {
    uint boneTransformBufferIndex;
    uint postSkinningVertexBufferOffset;
    uint pad[2];
};

#define LIGHTS_PER_PAGE 50
#define LIGHT_PAGE_ADDRESS_NULL 0xFFFFFFFF
struct LightPage {
    uint ptrNextPage;
    uint numLightsInPage;
    uint lightIndices[LIGHTS_PER_PAGE];
};

struct Cluster {
    float4 minPoint;
    float4 maxPoint;
    uint numLights;
    uint ptrFirstPage;
    uint pad[2];
};

#endif // __STRUCTS_HLSL__