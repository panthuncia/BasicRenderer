#pragma once
#include <DirectXMath.h>

struct ClippingPlane {
	DirectX::XMFLOAT4 plane;
};

struct CameraInfo {
    DirectX::XMFLOAT4 positionWorldSpace;
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX viewInverse;
    DirectX::XMMATRIX projection;
    DirectX::XMMATRIX projectionInverse;
	DirectX::XMMATRIX viewProjection;
	ClippingPlane clippingPlanes[6];
	float fov;
	float aspectRatio;
	float zNear;
	float zFar;
};

struct PerFrameCB {
    DirectX::XMVECTOR ambientLighting;
    DirectX::XMVECTOR shadowCascadeSplits;

	unsigned int mainCameraIndex;
	unsigned int activeLightIndicesBufferIndex;
    unsigned int lightBufferIndex;
    unsigned int numLights;

    unsigned int pointLightCubemapBufferIndex;
    unsigned int spotLightMatrixBufferIndex;
    unsigned int directionalLightCascadeBufferIndex;
	unsigned int numShadowCascades;

    unsigned int activeEnvironmentIndex;
    unsigned int environmentBufferDescriptorIndex;
	unsigned int environmentBRDFLUTIndex;
	unsigned int environmentBRDFLUTSamplerIndex;

    unsigned int outputType;
    unsigned int screenResX;
    unsigned int screenResY;
    unsigned int lightClusterGridSizeX;

    unsigned int lightClusterGridSizeY;
	unsigned int lightClusterGridSizeZ;
    unsigned int nearClusterCount; // how many uniform slices up close
    float clusterZSplitDepth; // view-space depth to switch to log

    unsigned int tonemapType;
    unsigned int pad[3];
};

struct PerObjectCB {
    DirectX::XMMATRIX modelMatrix;
    unsigned int normalMatrixBufferIndex;
    unsigned int pad[3];
};

struct BoundingSphere {
	DirectX::XMFLOAT4 sphere;
};

struct PerMeshCB {
    unsigned int materialDataIndex;
    unsigned int vertexFlags;
	unsigned int vertexByteSize;
    unsigned int skinningVertexByteSize;

    unsigned int inverseBindMatricesBufferIndex;
	unsigned int vertexBufferOffset;
    unsigned int meshletBufferOffset;
    unsigned int meshletVerticesBufferOffset;

    unsigned int meshletTrianglesBufferOffset;
	BoundingSphere boundingSphere;
	unsigned int numVertices;
    unsigned int numMeshlets;
    unsigned int pad[1];
};

struct PerMeshInstanceCB {
    unsigned int boneTransformBufferIndex;
    unsigned int postSkinningVertexBufferOffset;
	unsigned int meshletBoundsBufferStartIndex;
    unsigned int meshletBitfieldStartIndex;
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

    bool shadowCaster;
	BoundingSphere boundingSphere;
    float maxRange;
	unsigned int pad[2];
};

#define LIGHTS_PER_PAGE 12
struct LightPage {
    unsigned int ptrNextPage;
    unsigned int numLightsInPage;
    unsigned int lightIndices[LIGHTS_PER_PAGE];
};

struct Cluster {
    DirectX::XMVECTOR minPoint;
    DirectX::XMVECTOR maxPoint;
    unsigned int numLights;
    unsigned int ptrFirstPage;
    unsigned int pad[2];
};

typedef unsigned int uint;

namespace XeGTAO {
    struct GTAOConstants {
        DirectX::XMUINT2 ViewportSize;
        DirectX::XMFLOAT2 ViewportPixelSize; // .zw == 1.0 / ViewportSize.xy

        DirectX::XMFLOAT2 DepthUnpackConsts;
        DirectX::XMFLOAT2 CameraTanHalfFOV;

        DirectX::XMFLOAT2 NDCToViewMul;
        DirectX::XMFLOAT2 NDCToViewAdd;

        DirectX::XMFLOAT2 NDCToViewMul_x_PixelSize;
        float EffectRadius; // world (viewspace) maximum size of the shadow
        float EffectFalloffRange;

        float RadiusMultiplier;
        float Padding0;
        float FinalValuePower;
        float DenoiseBlurBeta;

        float SampleDistributionPower;
        float ThinOccluderCompensation;
        float DepthMIPSamplingOffset;
        int NoiseIndex; // frameIndex % 64 if using TAA or 0 otherwise
    };
}

struct GTAOInfo {
    XeGTAO::GTAOConstants g_GTAOConstants;

    uint g_samplerPointClampDescriptorIndex;
    uint g_srcRawDepthDescriptorIndex; // source depth buffer data (in NDC space in DirectX)
    uint g_outWorkingDepthMIP0DescriptorIndex; // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
    uint g_outWorkingDepthMIP1DescriptorIndex; // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
    
    uint g_outWorkingDepthMIP2DescriptorIndex; // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
    uint g_outWorkingDepthMIP3DescriptorIndex; // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
    uint g_outWorkingDepthMIP4DescriptorIndex; // output viewspace depth MIP (these are views into g_srcWorkingDepth MIP levels)
    // input output textures for the second pass (XeGTAO_MainPass)
    uint g_srcWorkingDepthDescriptorIndex; // viewspace depth with MIPs, output by XeGTAO_PrefilterDepths16x16 and consumed by XeGTAO_MainPass
    
    uint g_srcNormalmapDescriptorIndex; // source normal map
    uint g_srcHilbertLUTDescriptorIndex; // hilbert lookup table  (if any) (unused)
    uint g_outWorkingAOTermDescriptorIndex; // output AO term (includes bent normals if enabled - packed as R11G11B10 scaled by AO)// // Moved to root constant
    uint g_outWorkingEdgesDescriptorIndex; // output depth-based edges used by the denoiser
    
    uint g_outNormalmapDescriptorIndex; // output viewspace normals if generating from depth (unused)
    // input output textures for the third pass (XeGTAO_Denoise)
    //uint g_srcWorkingAOTermDescriptorIndex; // coming from previous pass // Moved to root constant
    uint g_srcWorkingEdgesDescriptorIndex; // coming from previous pass
    uint g_outFinalAOTermDescriptorIndex; // final AO term - just 'visibility' or 'visibility + bent normals'
    uint pad[1];
};

struct EnvironmentInfo {
	unsigned int cubeMapDescriptorIndex;
    unsigned int prefilteredCubemapDescriptorIndex;
    float sphericalHarmonicsScale;
    int sphericalHarmonics[27]; // Floats scaled by SH_FLOAT_SCALE
    uint pad[2];
};

enum RootSignatureLayout {
    PerObjectRootSignatureIndex,
    PerMeshRootSignatureIndex,
	ViewRootSignatureIndex,
	SettingsRootSignatureIndex,
	StaticBufferRootSignatureIndex,
	VariableBufferRootSignatureIndex,
	TransparencyInfoRootSignatureIndex,
	LightClusterRootSignatureIndex,
	MiscUintRootSignatureIndex,
	MiscFloatRootSignatureIndex,
	NumRootSignatureParameters
};

enum PerObjectRootConstants {
	PerObjectBufferIndex,
	NumPerObjectRootConstants
};

enum PerMeshRootConstants {
	PerMeshBufferIndex,
	PerMeshInstanceBufferIndex,
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
    EnableGTAO,
	NumSettingsRootConstants
};

enum StaticBufferRootConstants {
    PerMeshBufferDescriptorIndex,
    NormalMatrixBufferDescriptorIndex,
    PreSkinningVertexBufferDescriptorIndex,
    PostSkinningVertexBufferDescriptorIndex,
    MeshletBufferDescriptorIndex,
    MeshletVerticesBufferDescriptorIndex,
    MeshletTrianglesBufferDescriptorIndex,
    PerObjectBufferDescriptorIndex,
    CameraBufferDescriptorIndex,
    PerMeshInstanceBufferDescriptorIndex,
    DrawSetCommandBufferDescriptorIndex,
	NormalsTextureDescriptorIndex,
    AOTextureDescriptorIndex,
	AlbedoTextureDescriptorIndex,
	MetallicRoughnessTextureDescriptorIndex,
	EmissiveTextureDescriptorIndex,
    NumStaticBufferRootConstants,
};

enum VariableBufferRootConstants {
    ActiveDrawSetIndicesBufferDescriptorIndex,
    IndirectCommandBufferDescriptorIndex,
    MeshletCullingIndirectCommandBufferDescriptorIndex,
    MeshletCullingBitfieldBufferDescriptorIndex,
    MaxDrawIndex,
	NumVariableBufferRootConstants
};

enum TransparencyInfoRootConstants {
	PPLLHeadBufferDescriptorIndex,
	PPLLNodeBufferDescriptorIndex,
	PPLLCounterBufferDescriptorIndex,
    PPLLNodePoolSize,
	NumTransparencyInfoRootConstants
};

enum LightClusterRootConstants {
	LightClusterBufferDescriptorIndex,
	LightPagesBufferDescriptorIndex,
	LightPagesCounterDescriptorIndex,
    LightPagesPoolSize,
	NumLightClusterRootConstants
};

enum MiscUintRootConstants { // Used for pass-specific one-off constants
    UintRootConstant0,
    UintRootConstant1,
    UintRootConstant2,
    UintRootConstant3,
	UintRootConstant4,
	NumMiscUintRootConstants
};

enum MiscFloatRootConstants { // Used for pass-specific one-off constants
	FloatRootConstant0,
	FloatRootConstant1,
	NumMiscFloatRootConstants
};