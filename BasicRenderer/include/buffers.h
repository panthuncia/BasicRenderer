#pragma once
#include <DirectXMath.h>

struct ClippingPlane {
	DirectX::XMFLOAT4 plane;
};

struct CameraInfo {
    DirectX::XMFLOAT4 positionWorldSpace;
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX projection;
	DirectX::XMMATRIX viewProjection;
	ClippingPlane clippingPlanes[6];
};

struct PerFrameCB {
    DirectX::XMVECTOR ambientLighting;
    DirectX::XMVECTOR shadowCascadeSplits;
	unsigned int mainCameraIndex;
    unsigned int lightBufferIndex;
    unsigned int numLights;
    unsigned int pointLightCubemapBufferIndex;
    unsigned int spotLightMatrixBufferIndex;
    unsigned int directionalLightCascadeBufferIndex;
	unsigned int numShadowCascades;
	unsigned int environmentIrradianceMapIndex;
	unsigned int environmentIrradianceSamplerIndex;
	unsigned int environmentPrefilteredMapIndex;
	unsigned int environmentPrefilteredSamplerIndex;
	unsigned int environmentBRDFLUTIndex;
	unsigned int environmentBRDFLUTSamplerIndex;
    unsigned int outputType;
	unsigned int pad[2];
};

struct PerObjectCB {
    DirectX::XMMATRIX modelMatrix;
    unsigned int normalMatrixBufferIndex;
    unsigned int pad[3];
};

struct BoundingSphere {
	DirectX::XMFLOAT4 center;
	float radius;
};

struct PerMeshCB {
    unsigned int materialDataIndex;
    unsigned int vertexFlags;
	unsigned int vertexByteSize;
    unsigned int skinningVertexByteSize;
	unsigned int preSkinningVertexBufferOffset;
    unsigned int postSkinningVertexBufferOffset;
    unsigned int meshletBufferOffset;
    unsigned int meshletVerticesBufferOffset;
    unsigned int meshletTrianglesBufferOffset;
	BoundingSphere boundingSphere;
	unsigned int numVertices;
    unsigned int boneTransformBufferIndex;
    unsigned int inverseBindMatricesBufferIndex;
    unsigned int pad[3];
};

struct PerMaterialCB {
    unsigned int materialFlags;
    unsigned int baseColorTextureIndex;
    unsigned int baseColorSamplerIndex;
    unsigned int normalTextureIndex;
    unsigned int normalSamplerIndex;
    unsigned int metallicTextureIndex;
    unsigned int metallicSamplerIndex;
	unsigned int roughnessTextureIndex;
	unsigned int roughnessSamplerIndex;
    unsigned int emissiveTextureIndex;
    unsigned int emissiveSamplerIndex;
    unsigned int aoMapIndex;
    unsigned int aoSamplerIndex;
    unsigned int heightMapIndex;
    unsigned int heightSamplerIndex;
    float metallicFactor;
    float roughnessFactor;
    float ambientStrength;
    float specularStrength;
    float textureScale;
    float heightMapScale;
    float alphaCutoff;
    unsigned int pad0;
    unsigned int pad1;
    DirectX::XMFLOAT4 baseColorFactor;
    DirectX::XMFLOAT4 emissiveFactor;
};

struct LightInfo {
    // Light attributes: x=type (0=point, 1=spot, 2=directional)
    // x=point -> w = shadow caster
    // x=spot -> y= inner cone angle, z= outer cone angle, w= shadow caster
    // x=directional => w= shadow caster
    unsigned int type;
    float innerConeAngle;
    float outerConeAngle;
    int shadowViewInfoIndex;
    DirectX::XMVECTOR posWorldSpace; // Position of the lights
    DirectX::XMVECTOR dirWorldSpace; // Direction of the lights
    DirectX::XMVECTOR attenuation; // x,y,z = constant, linear, quadratic attenuation
    DirectX::XMVECTOR color; // Color of the lights
    float nearPlane;
    float farPlane;
	int shadowMapIndex = -1;
    int shadowSamplerIndex = -1;
};

enum RootSignatureLayout {
    PerObjectRootSignatureIndex,
    PerMeshRootSignatureIndex,
	ViewRootSignatureIndex,
	SettingsRootSignatureIndex,
	StaticBufferRootSignatureIndex,
	VariableBufferRootSignatureIndex,
	TransparencyInfoRootSignatureIndex,
	NumRootSignatureParameters
};

enum PerObjectRootConstants {
	PerObjectBufferIndex,
	NumPerObjectRootConstants
};

enum PerMeshRootConstants {
	PerMeshBufferIndex,
	NumPerMeshRootConstants
};

enum ViewRootConstants {
	CurrentLightID,
    LightViewIndex,
	NumViewRootConstants
};

enum SettingsRootConstants {
	EnableShadows,
	EnablePunctualLights,
	NumSettingsRootConstants
};

enum StaticBufferRootConstants {
    NormalMatrixBufferDescriptorIndex,
    PreSkinningVertexBufferDescriptorIndex,
    PostSkinningVertexBufferDescriptorIndex,
    MeshletBufferDescriptorIndex,
    MeshletVerticesBufferDescriptorIndex,
    MeshletTrianglesBufferDescriptorIndex,
    PerObjectBufferDescriptorIndex,
    CameraBufferDescriptorIndex,
	NumStaticBufferRootConstants
};

enum VariableBufferRootConstants {
    PerMeshBufferDescriptorIndex,
    DrawSetCommandBufferDescriptorIndex,
    ActiveDrawSetIndicesBufferDescriptorIndex,
    IndirectCommandBufferDescriptorIndex,
    MaxDrawIndex,
	NumVariableBufferRootConstants
};

enum TransparencyInfoRootConstants {
	PPLLHeadBufferDescriptorIndex,
	PPLLNodeBufferDescriptorIndex,
	PPLLCounterBufferDescriptorIndex,
	PPLLNodeSize,
	NumTransparencyInfoRootConstants
};