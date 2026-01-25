#pragma once
#include <DirectXMath.h>
#include "ThirdParty/meshoptimizer/clusterlod.h"

struct ClippingPlane {
	DirectX::XMFLOAT4 plane;
};

struct CameraInfo {
    DirectX::XMFLOAT4 positionWorldSpace;
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX viewInverse;
    DirectX::XMMATRIX jitteredProjection;
    DirectX::XMMATRIX projectionInverse;
	DirectX::XMMATRIX viewProjection;

    DirectX::XMMATRIX prevView;
	DirectX::XMMATRIX prevJitteredProjection;

    DirectX::XMMATRIX unjitteredProjection;

	ClippingPlane clippingPlanes[6];

	float fov;
	float aspectRatio;
	float zNear;
	float zFar;

    int depthBufferArrayIndex = -1;
    unsigned int depthResX;
	unsigned int depthResY;
    uint16_t numDepthMips;

    unsigned int isOrtho = 0; // bool
	DirectX::XMFLOAT2 uvScaleToNextPowerOfTwo = { 1.0f, 1.0f }; // Scale to next power of two, for linear depth buffer
    unsigned int pad[1];
};

struct CullingCameraInfo {
    DirectX::XMFLOAT4 positionWorldSpace;
    float projY = 0.0f;
	float zNear = 0.0f;
	float errorPixels = 0.0f; // Target error in pixels for LOD calculations
	float pad[1];
};

struct PerFrameCB {
    DirectX::XMVECTOR ambientLighting;
    DirectX::XMVECTOR shadowCascadeSplits;

	unsigned int mainCameraIndex;
	//unsigned int activeLightIndicesBufferIndex;
    //unsigned int lightBufferIndex;
    unsigned int numLights;

    //unsigned int pointLightCubemapBufferIndex;
    //unsigned int spotLightMatrixBufferIndex;
    //unsigned int directionalLightCascadeBufferIndex;
	unsigned int numShadowCascades;

    unsigned int activeEnvironmentIndex;
    //unsigned int environmentBufferDescriptorIndex;
	
    //unsigned int environmentBRDFLUTIndex;
	//unsigned int environmentBRDFLUTSamplerIndex;

    unsigned int outputType;
    unsigned int screenResX;
    unsigned int screenResY;
    unsigned int lightClusterGridSizeX;

    unsigned int lightClusterGridSizeY;
	unsigned int lightClusterGridSizeZ;
    unsigned int nearClusterCount; // how many uniform slices up close
    float clusterZSplitDepth; // view-space depth to switch to log

    unsigned int frameIndex; // 0 to 63
    unsigned int pad[3];
};

struct PerObjectCB {
    DirectX::XMMATRIX modelMatrix;
    DirectX::XMMATRIX prevModelMatrix;
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

	unsigned int vertexBufferOffset;
    unsigned int meshletBufferOffset;
    unsigned int meshletVerticesBufferOffset;
    unsigned int meshletTrianglesBufferOffset;

	BoundingSphere boundingSphere;

	unsigned int numVertices;
    unsigned int numMeshlets;
    unsigned int pad[2];
};

struct PerMeshInstanceCB {
    unsigned int perMeshBufferIndex;
    unsigned int perObjectBufferIndex;
    unsigned int skinningInstanceSlot;
    unsigned int postSkinningVertexBufferOffset;
	unsigned int meshletBoundsBufferStartIndex;
    unsigned int meshletBitfieldStartIndex;
	unsigned int clusterToVisibleClusterTableStartIndex;
	unsigned int pad[1];
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
	unsigned int opacityTextureIndex;
	
    unsigned int opacitySamplerIndex;
    float metallicFactor;
    float roughnessFactor;
    float ambientStrength;
    
    float specularStrength;
    float textureScale;
    float heightMapScale;
    float alphaCutoff;

    DirectX::XMFLOAT4 baseColorFactor;
    DirectX::XMFLOAT4 emissiveFactor;
    DirectX::XMUINT4 baseColorChannels;

    DirectX::XMUINT3 normalChannels;
    unsigned int compileFlagsID;

	unsigned int aoChannel;
    unsigned int heightChannel;
    unsigned int metallicChannel;
    unsigned int roughnessChannel;
    
    DirectX::XMUINT3 emissiveChannels;
	float pad0;
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

struct LPMConstants {
    unsigned int u_ctl[24 * 4];
    unsigned int shoulder;
    unsigned int con;
    unsigned int soft;
    unsigned int con2;
    unsigned int clip;
    unsigned int scaleOnly;
    uint displayMode;
    uint pad;
    DirectX::XMMATRIX inputToOutputMatrix;
};

struct VisibleClusterInfo {
    DirectX::XMUINT2 drawcallIndexAndMeshletIndex; // .x = drawcall index, .y = meshlet local index
    unsigned int pad[2];
};

struct SkinningInstanceGPUInfo {
    uint32_t transformOffsetMatrices = 0;
    uint32_t invBindOffsetMatrices = 0;
    uint32_t boneCount = 0;
    uint32_t _pad = 0;
};

struct MeshInstanceClodOffsets
{
    uint groupsBase;
    uint childrenBase;
    uint childLocalMeshletIndicesBase;
    uint meshletsBase;

    uint meshletBoundsBase;
    uint lodNodesBase;
    uint rootNode; // node index (relative to lodNodesBase) to start traversal from
    uint pad[1];
};

// Cluster LOD data
// One entry per (group -> refinedGroup) edge.
// refinedGroup == -1 means "terminal meshlets" (original geometry)
struct ClusterLODChild
{
    int32_t  refinedGroup;              // group id to refine into, or -1
    uint32_t firstLocalMeshletIndex;     // offset into m_clodChildLocalMeshletIndices
    uint32_t localMeshletCount;          // number of local meshlets in this child bucket
    uint32_t pad = 0;
};

struct ClusterLODGroup
{
    clodBounds bounds; // 5 floats
	float pad0[3]; // pad to 32 bytes
    uint32_t firstMeshlet = 0;
    uint32_t meshletCount = 0;
    int32_t depth = 0;

    uint32_t firstChild = 0;    // offset into m_clodChildren
    uint32_t childCount = 0;    // number of ClusterLODChild entries for this group
    uint32_t pad[2] = { 0, 0 };
};

struct VisibleCluster {
    unsigned int viewID;
    unsigned int instanceID;
    unsigned int meshletID;
	unsigned int pad;
};


enum RootSignatureLayout {
    PerObjectRootSignatureIndex,
    PerMeshRootSignatureIndex,
	ViewRootSignatureIndex,
	SettingsRootSignatureIndex,
	DrawInfoRootSignatureIndex,
	TransparencyInfoRootSignatureIndex,
	LightClusterRootSignatureIndex,
	MiscUintRootSignatureIndex,
	MiscFloatRootSignatureIndex,
	ResourceDescriptorIndicesRootSignatureIndex,
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

enum DrawInfoRootConstants {
    MaxDrawIndex,
	NumDrawInfoRootConstants
};

enum TransparencyInfoRootConstants {
    PPLLNodePoolSize,
	NumTransparencyInfoRootConstants
};

enum LightClusterRootConstants {
    LightPagesPoolSize,
	NumLightClusterRootConstants
};

enum MiscUintRootConstants { // Used for pass-specific one-off constants
    UintRootConstant0,
    UintRootConstant1,
    UintRootConstant2,
    UintRootConstant3,
	UintRootConstant4,
	UintRootConstant5,
	UintRootConstant6,
	UintRootConstant7,
	UintRootConstant8,
	UintRootConstant9,
	UintRootConstant10,
	NumMiscUintRootConstants
};

enum MiscFloatRootConstants { // Used for pass-specific one-off constants
	FloatRootConstant0,
	FloatRootConstant1,
	FloatRootConstant2,
	FloatRootConstant3,
	FloatRootConstant4,
	FloatRootConstant5,
	NumMiscFloatRootConstants
};

enum ResourceDescriptorIndicesRootConstants {
    ResourceDescriptorIndex0,
    ResourceDescriptorIndex1,
	ResourceDescriptorIndex2,
	ResourceDescriptorIndex3,
	ResourceDescriptorIndex4,
	ResourceDescriptorIndex5,
	ResourceDescriptorIndex6,
	ResourceDescriptorIndex7,
	ResourceDescriptorIndex8,
	ResourceDescriptorIndex9,
	ResourceDescriptorIndex10,
	ResourceDescriptorIndex11,
	ResourceDescriptorIndex12,
	ResourceDescriptorIndex13,
	ResourceDescriptorIndex14,
	ResourceDescriptorIndex15,
	ResourceDescriptorIndex16,
	ResourceDescriptorIndex17,
	ResourceDescriptorIndex18,
	ResourceDescriptorIndex19,
	ResourceDescriptorIndex20,
	ResourceDescriptorIndex21,
	ResourceDescriptorIndex22,
	ResourceDescriptorIndex23,
	ResourceDescriptorIndex24,
	ResourceDescriptorIndex25,
	ResourceDescriptorIndex26,
	ResourceDescriptorIndex27,
	ResourceDescriptorIndex28,
	ResourceDescriptorIndex29,
	ResourceDescriptorIndex30,
	ResourceDescriptorIndex31,
	ResourceDescriptorIndex32,
	ResourceDescriptorIndex33,
	ResourceDescriptorIndex34,
    NumResourceDescriptorIndicesRootConstants
};