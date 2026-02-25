#ifndef __STRUCTS_HLSL__
#define __STRUCTS_HLSL__

struct PSInput {
    float4 position : SV_POSITION; // Screen-space position, required for rasterization
    float4 clipPosition : TEXCOORD0;
    float4 prevClipPosition : TEXCOORD1; // Previous frame position for motion vectors
    float4 positionWorldSpace : TEXCOORD2; // For world-space lighting
    float4 positionViewSpace : TEXCOORD3; // For cascaded shadows
    float3 normalWorldSpace : TEXCOORD4; // For world-space lighting
    float2 texcoord : TEXCOORD5;
    float3 color : TEXCOORD6; // For models with vertex colors
    float3 normalModelSpace : TEXCOORD7; // For debug view
    uint meshletIndex : TEXCOORD8; // For meshlet debug view
};

struct VisBufferPSInput
{
    float4 position : SV_POSITION; // Screen-space position, required for rasterization
    float linearDepth : TEXCOORD0;
#if defined (PSO_ALPHA_TEST)
    float2 texcoord : TEXCOORD1;
    nointerpolation uint materialDataIndex : TEXCOORD2; // convenience for alpha test
#endif
    nointerpolation uint visibleClusterIndex : TEXCOORD3;
    nointerpolation uint viewID : TEXCOORD4;
};

struct ClodViewRasterInfo
{
    uint visibilityUAVDescriptorIndex;
    uint scissorMinX;
    uint scissorMinY;
    uint scissorMaxX;
    uint scissorMaxY;
    float viewportScaleX;
    float viewportScaleY;
    uint pad0;
};

struct ClippingPlane {
    float4 plane;
};

struct Camera {
    float4 positionWorldSpace;
    row_major matrix view;
    row_major matrix viewInverse;
    row_major matrix projection;
    row_major matrix projectionInverse;
    row_major matrix viewProjection;
    
    row_major matrix prevView;
    row_major matrix prevJitteredProjection;
    
    row_major matrix unjitteredProjection;

    ClippingPlane clippingPlanes[6];
    
    float fov;
    float aspectRatio;
    float zNear;
    float zFar;

    int depthBufferArrayIndex;
    uint depthResX;
    uint depthResY;
    uint numDepthMips;
    
    bool isOrtho;
    float2 UVScaleToNextPowerOf2;
    uint pad[1];
};

struct CullingCameraInfo
{
    float4 positionWorldSpace;
    float projY;
    float zNear;
    float errorPixels; // Target error in pixels for LOD calculations
    float pad;
};

struct PerFrameBuffer {
    float4 ambientLighting;
    float4 shadowCascadeSplits;
    
    uint mainCameraIndex;
    uint numLights;
    uint numShadowCascades;
    
    unsigned int activeEnvironmentIndex;
    
    uint outputType;
    uint screenResX;
    uint screenResY;
    uint lightClusterGridSizeX;
    
    uint lightClusterGridSizeY;
    uint lightClusterGridSizeZ;
    uint nearClusterCount; // how many uniform slices up close
    float clusterZSplitDepth; // view-space depth to switch to log
    
    uint frameIndex; // 0 to 64
    uint pad[3];
};

struct BoundingSphere {
    float4 sphere;
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
    uint pad[2];
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
    uint opacityTextureIndex;
    
    uint opacitySamplerIndex;
    float metallicFactor;
    float roughnessFactor;
    float ambientStrength;
    
    float specularStrength;
    float textureScale;
    float heightMapScale;
    float alphaCutoff;
    
    float4 baseColorFactor;
    float4 emissiveFactor;
    
    uint4 baseColorChannels;
    
    uint3 normalChannels;
    uint compileFlagsID;
    
    uint aoChannel;
    uint heightChannel;
    uint metallicChannel;
    uint roughnessChannel;

    uint3 emissiveChannels;
    uint rasterBucketIndex;
};

struct SingleMatrix {
    row_major matrix value;
};

struct PerObjectBuffer {
    row_major matrix model;
    row_major matrix prevModel;
    uint normalMatrixBufferIndex;
    uint pad[3];
};

struct PerMeshBuffer {
    uint materialDataIndex;
    uint vertexFlags;
    uint vertexByteSize;
    uint skinningVertexByteSize;

    BoundingSphere boundingSphere;
    
    uint clodMeshletBufferOffset;
    uint clodMeshletVerticesBufferOffset;
    uint clodMeshletTrianglesBufferOffset;
    uint clodNumMeshlets;
    
    uint vertexBufferOffset;
    uint numVertices;
    uint numMeshlets;
    uint pad[1];
};

struct PerMeshInstanceBuffer {
    uint perMeshBufferIndex;
    uint perObjectBufferIndex;
    uint skinningInstanceSlot;
    uint postSkinningVertexBufferOffset;
};

#define LIGHTS_PER_PAGE 12
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

static const uint CLOD_REPLAY_RECORD_TYPE_NODE = 0;
static const uint CLOD_REPLAY_RECORD_TYPE_GROUP = 1;
static const uint CLOD_REPLAY_BUFFER_SIZE_BYTES = 8u * 1024u * 1024u;

struct CLodNodeGroupReplayRecord
{
    uint type;
    uint instanceIndex;
    uint viewId;
    uint nodeOrGroupId;
};

struct CLodMeshletReplayRecord
{
    uint instanceIndex;
    uint viewId;
    uint groupId;
    uint localMeshletIndex;
};

struct CLodReplayBufferState
{
    uint nodeGroupWriteOffsetBytes;
    uint meshletWriteOffsetBytes;
    uint nodeGroupDroppedRecords;
    uint meshletDroppedRecords;
};

struct CLodViewDepthSRVIndex
{
    uint cameraBufferIndex;
    uint linearDepthSRVIndex;
    uint pad0;
    uint pad1;
};

struct CLodNodeGpuInput
{
    uint entrypointIndex;
    uint numRecords;
    uint64_t recordsAddress;
    uint64_t recordStride;
};

struct CLodMultiNodeGpuInput
{
    uint numNodeInputs;
    uint pad0;
    uint64_t nodeInputsAddress;
    uint64_t nodeInputStride;
};

struct GTAOConstants {
    uint2 ViewportSize;
    float2 ViewportPixelSize; // .zw == 1.0 / ViewportSize.xy

    float2 DepthUnpackConsts;
    float2 CameraTanHalfFOV;

    float2 NDCToViewMul;
    float2 NDCToViewAdd;

    float2 NDCToViewMul_x_PixelSize;
    float EffectRadius; // world (viewspace) maximum size of the shadow
    float EffectFalloffRange;

    float RadiusMultiplier;
    float2 SourceDepthUVScale;
    float Padding0;
    float FinalValuePower;
    float DenoiseBlurBeta;

    float SampleDistributionPower;
    float ThinOccluderCompensation;
    float DepthMIPSamplingOffset;
    int NoiseIndex; // frameIndex % 64 if using TAA or 0 otherwise
};

struct GTAOInfo {
    GTAOConstants g_GTAOConstants;
};

struct FragmentInfo {
    float2 pixelCoords;
    float3 fragPosWorldSpace;
    float3 fragPosViewSpace;
    float3 normalWS;
    float3 diffuseColor;
    float3 albedo;
    float alpha;
    float diffuseAmbientOcclusion;
    float metallic;
    float perceptualRoughnessUnclamped;
    float perceptualRoughness;
    float roughness;
    float roughnessUnclamped;
    float3 emissive;
    //float2 DFG; // Replaced by MaterialX quadratic fit
    float3 viewWS;
    float NdotV;
    float reflectance;
    float dielectricF0;
    float3 F0;
    float3 reflectedWS;
    uint heightMapIndex;
    uint heightMapSamplerIndex;
    uint materialFlags;
};

struct EnvironmentInfo {
    uint cubeMapDescriptorIndex;
    uint prefilteredCubemapDescriptorIndex;
    float sphericalHarmonicsScale;
    int sphericalHarmonics[27]; // floats scaled by SH_FLOAT_SCALE
    uint pad[2];
};

struct LPMConstants
{
    uint u_ctl[24 * 4];
    uint shoulder;
    uint con;
    uint soft;
    uint con2;
    uint clip;
    uint scaleOnly;
    uint displayMode;
    uint pad;
    float4x4 inputToOutputMatrix;
};

struct MaterialInputs
{
    float3 albedo;
    float3 normalWS;
    float3 emissive;
    float metallic;
    float roughness;
    float opacity;
    float ambientOcclusion;
};

struct SkinningInstanceGPUInfo
{
    uint transformOffsetMatrices;
    uint invBindOffsetMatrices;
    uint boneCount;
    uint pad[1];
};

// TODO: packing?
/*
struct ClusterCandidateNode
{
    uint viewIndex;

    uint perMeshBufferIndex;
    uint perMeshInstanceBufferIndex;
    uint perObjectBufferIndex;

    uint rootGroupGlobal; // absolute group index in global groups buffer
    uint flags; // bits: fullyInside, skipFrustum, wasVisibleLastFrame, etc.
};*/

struct VisibleCluster
{
    unsigned int viewID;
    unsigned int instanceID;
    unsigned int globalMeshletIndex;
    unsigned int groupID;
};

#endif // __STRUCTS_HLSL__