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
    row_major matrix prevModel;
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
    uint numMeshlets;
    uint pad[1];
};

struct PerMeshInstanceBuffer {
    uint boneTransformBufferIndex;
    uint postSkinningVertexBufferOffset;
    uint meshletBoundsBufferStartIndex;
    uint meshletBitfieldStartIndex;
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
    uint g_outWorkingAOTermDescriptorIndex; // output AO term (includes bent normals if enabled - packed as R11G11B10 scaled by AO)
    uint g_outWorkingEdgesDescriptorIndex; // output depth-based edges used by the denoiser
    
    uint g_outNormalmapDescriptorIndex; // output viewspace normals if generating from depth (unused)
    // input output textures for the third pass (XeGTAO_Denoise)
    //uint g_srcWorkingAOTermDescriptorIndex; // coming from previous pass // Moved to root constant
    uint g_srcWorkingEdgesDescriptorIndex; // coming from previous pass
    uint g_outFinalAOTermDescriptorIndex; // final AO term - just 'visibility' or 'visibility + bent normals'
    uint pad[1];
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

#endif // __STRUCTS_HLSL__