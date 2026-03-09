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
    uint32_t numDepthMips;

    unsigned int isOrtho = 0; // bool
	DirectX::XMFLOAT2 uvScaleToNextPowerOfTwo = { 1.0f, 1.0f }; // Scale to next power of two, for linear depth buffer
    unsigned int pad[1];
};

struct CullingCameraInfo {
    DirectX::XMFLOAT4 positionWorldSpace;
    float projY = 0.0f;
	float zNear = 0.0f;
    float errorOverDistanceThreshold = 0.0f; // Threshold for (error * scale) / distance metric
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

// Object flags (shared with HLSL OBJECT_FLAG_* defines)
static constexpr unsigned int OBJECT_FLAG_REVERSE_WINDING = 1u << 0;

struct PerObjectCB {
    DirectX::XMMATRIX modelMatrix;
    DirectX::XMMATRIX prevModelMatrix;
    unsigned int normalMatrixBufferIndex;
    unsigned int objectFlags;
    unsigned int pad[2];
};

struct BoundingSphere {
	DirectX::XMFLOAT4 sphere;
};

struct PerMeshCB {
    unsigned int materialDataIndex;
    unsigned int vertexFlags;
	unsigned int vertexByteSize;
    unsigned int skinningVertexByteSize;

	BoundingSphere boundingSphere;

    unsigned int clodMeshletBufferOffset;
    unsigned int clodMeshletVerticesBufferOffset;
    unsigned int clodMeshletTrianglesBufferOffset;
    unsigned int clodNumMeshlets;

    unsigned int vertexBufferOffset;
    unsigned int numVertices;
    unsigned int numMeshlets;
    unsigned int pad[1];
};

struct PerMeshInstanceCB {
    unsigned int perMeshBufferIndex;
    unsigned int perObjectBufferIndex;
    unsigned int skinningInstanceSlot;
    unsigned int postSkinningVertexBufferOffset;
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
	unsigned int rasterBuckedIndex;
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

        DirectX::XMFLOAT2 DepthUnpackConsts; // UNUSED if source depth is linear view depth.
        DirectX::XMFLOAT2 CameraTanHalfFOV;

        DirectX::XMFLOAT2 NDCToViewMul;
        DirectX::XMFLOAT2 NDCToViewAdd;

        DirectX::XMFLOAT2 NDCToViewMul_x_PixelSize;
        float EffectRadius; // world (viewspace) maximum size of the shadow
        float EffectFalloffRange;

        float RadiusMultiplier;
        DirectX::XMFLOAT2 SourceDepthUVScale; // Scale UVs when sampling source depth if texture is padded (e.g. to next power-of-two).
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
    uint clodMeshMetadataIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};

struct CLodMeshMetadata
{
    uint groupsBase;
    uint childrenBase;
    uint lodNodesBase;
    uint rootNode; // node index (relative to lodNodesBase) to start traversal from
    uint groupChunkTableBase;
    uint groupChunkTableCount;
};

/// GPU-visible page table entry — maps a virtual page ID to a slab + byte offset.
struct PageTableEntry
{
    uint32_t slabIndex      = 0; ///< Which slab ByteAddressBuffer this page lives in.
    uint32_t slabByteOffset = 0; ///< Byte offset of the page start within that slab.
};

struct ClusterLODGroupChunk
{
    // ── Group metadata ────────────────────────────────────────────────────
    uint32_t groupVertexCount = 0;
    uint32_t meshletVertexCount = 0;
    uint32_t meshletBase = 0;
    uint32_t meshletCount = 0;
    uint32_t meshletTrianglesByteCount = 0;
    uint32_t meshletBoundsBase = 0;
    uint32_t meshletBoundsCount = 0;

    // Compressed group-local position stream (u32 bitstream words)
    uint32_t compressedPositionWordCount = 0;
    uint32_t compressedPositionBitsX = 0;
    uint32_t compressedPositionBitsY = 0;
    uint32_t compressedPositionBitsZ = 0;
    uint32_t compressedPositionQuantExp = 0;
    int32_t compressedPositionMinQx = 0;
    int32_t compressedPositionMinQy = 0;
    int32_t compressedPositionMinQz = 0;

    // Compressed group-local normal stream (oct-encoded snorm16x2 packed into u32)
    uint32_t compressedNormalWordCount = 0;

    // Compressed group-local meshlet vertex index stream (u32 bitstream words)
    uint32_t compressedMeshletVertexWordCount = 0;
    uint32_t compressedMeshletVertexBits = 0;
    uint32_t compressedFlags = 0;

    // ── Page-pool fields ────────────────────────────────────────────────
    // The 6 large data streams (vertices, meshlet vertex indices, triangles,
    // compressed positions/normals/meshlet vertices) are stored in a
    // unified slab ByteAddressBuffer.  Meshlets and bounds stay in their
    // global StructuredBuffers.
    //
    // The slab descriptor index + base byte offset are resolved on the CPU
    // and stored here so the shader can bind the slab directly without a
    // page-table lookup.
    uint32_t pagePoolSlabDescriptorIndex = 0;         ///< Raw descriptor-heap index of the slab BAB.
    uint32_t pagePoolSlabByteOffset = 0;              ///< Byte offset of allocation start within the slab.
    uint32_t vertexIntraPageByteOffset = 0;           ///< Byte offset within allocation for vertex data.
    uint32_t meshletVertexIntraPageByteOffset = 0;    ///< Byte offset within allocation for meshlet vertex indices.
    uint32_t triangleIntraPageByteOffset = 0;         ///< Byte offset within allocation for triangle data.
    uint32_t compPosIntraPageByteOffset = 0;          ///< Byte offset within allocation for compressed positions.
    uint32_t compNormIntraPageByteOffset = 0;         ///< Byte offset within allocation for compressed normals.
    uint32_t compMeshletVertIntraPageByteOffset = 0;  ///< Byte offset within allocation for compressed meshlet vertex indices.
    uint32_t pagePoolPad0 = 0;                        ///< Padding to 16-byte alignment.
    uint32_t pagePoolPad1 = 0;                        ///< Padding.
    uint32_t pagePoolPad2 = 0;                        ///< Padding.
    uint32_t pagePoolPad3 = 0;                        ///< Padding (was pagePoolFlags).
};

struct CLodStreamingRequest
{
    uint32_t groupGlobalIndex = 0;
    uint32_t meshInstanceIndex = 0;
    uint32_t meshBufferIndex = 0;
    uint32_t viewId = 0; // low 16 bits: viewId, high 16 bits: quantized priority
};

struct CLodStreamingRuntimeState
{
    uint32_t activeGroupScanCount = 0;
    uint32_t unloadAfterFrames = 120;
    uint32_t activeGroupsBitsetWordCount = 0;
    uint32_t pad2 = 0;
};

// Cluster LOD data
// One entry per (group -> refinedGroup) edge.
// refinedGroup == -1 means "terminal meshlets" (original geometry)
struct ClusterLODChild
{
    int32_t  refinedGroup;              // group id to refine into, or -1
    uint32_t firstLocalMeshletIndex;     // group-local start meshlet index for this child bucket
    uint32_t localMeshletCount;          // number of local meshlets in this contiguous child range
    uint32_t pad = 0;
};

struct ClusterLODGroup
{
    clodBounds bounds; // 5 floats
    uint32_t firstMeshlet = 0;
    uint32_t meshletCount = 0;
    int32_t depth = 0;

	uint32_t firstGroupVertex = 0;
    uint32_t groupVertexCount = 0;
    uint32_t firstChild = 0;    // offset into m_clodChildren
    uint32_t childCount = 0;    // number of ClusterLODChild entries for this group

    uint32_t terminalChildCount = 0;
    uint32_t flags = 0;         // Bit 0: IS_VOXEL_GROUP
    uint32_t pad[2] = {0};
};

static constexpr uint32_t CLOD_GROUP_FLAG_IS_VOXEL = 1u << 0;

enum class CLodReplayRecordType : uint32_t {
    Node = 0,
    Group = 1,
    Meshlet = 2,
};

struct CLodNodeGroupReplayRecord {
    uint32_t type = 0; // CLodReplayRecordType
    uint32_t instanceIndex = 0;
    uint32_t viewId = 0;
    uint32_t nodeOrGroupId = 0;
    uint32_t pad0 = 0;
};

struct CLodMeshletReplayRecord {
    uint32_t type = 0; // CLodReplayRecordType
    uint32_t instanceIndex = 0;
    uint32_t viewId = 0;
    uint32_t groupId = 0;
    uint32_t localMeshletIndex = 0;
};

struct CLodReplayBufferState {
    uint32_t totalWriteCount = 0;
    uint32_t droppedRecords = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
};

struct CLodViewDepthSRVIndex {
    uint32_t cameraBufferIndex = 0;
    uint32_t linearDepthSRVIndex = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
};

struct CLodNodeGpuInput {
    uint32_t entrypointIndex = 0;
    uint32_t numRecords = 0;
    uint64_t recordsAddress = 0;
    uint64_t recordStride = 0;
};

struct CLodMultiNodeGpuInput {
    uint32_t numNodeInputs = 0;
    uint32_t pad0 = 0;
    uint64_t nodeInputsAddress = 0;
    uint64_t nodeInputStride = 0;
};

struct VisibleCluster {
    unsigned int viewID;
    unsigned int instanceID;
    unsigned int globalMeshletIndex;
    unsigned int groupID;
};


enum RootSignatureLayout {
    PerObjectRootSignatureIndex,
    PerMeshRootSignatureIndex,
	ViewRootSignatureIndex,
	SettingsRootSignatureIndex,
	MiscUintRootSignatureIndex,
	MiscFloatRootSignatureIndex,
	ResourceDescriptorIndicesRootSignatureIndex,
	IndirectCommandSignatureRootSignatureIndex,
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
    UintRootConstant11,
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

enum ResourceDescriptorIndicesRootConstants { // Auto-assigned, do not set manually
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

// Used for root constants in indirect command signatures.
// You can technically use these as regular root constants as well
enum IndirectCommandSignatureRootConstants {
    IndirectCommandSignatureRootConstant0,
    IndirectCommandSignatureRootConstant1,
    IndirectCommandSignatureRootConstant2,
    IndirectCommandSignatureRootConstant3,
    NumIndirectCommandSignatureRootConstants
};