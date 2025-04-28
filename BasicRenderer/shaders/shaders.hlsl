#include "vertex.hlsli"
#include "utilities.hlsli"
#include "cbuffers.hlsli"
#include "structs.hlsli"
#include "materialflags.hlsli"
#include "lighting.hlsli"
#include "tonemapping.hlsli"
#include "gammaCorrection.hlsli"
#include "outputTypes.hlsli"
#include "MaterialFlags.hlsli"

PSInput VSMain(uint vertexID : SV_VertexID) {
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[postSkinningVertexBufferDescriptorIndex];
    
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[perMeshInstanceBufferDescriptorIndex];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[perObjectBufferDescriptorIndex];
    PerObjectBuffer objectBuffer = perObjectBuffer[perObjectBufferIndex];
        
    uint byteOffset = meshInstanceBuffer.postSkinningVertexBufferOffset + vertexID * meshBuffer.vertexByteSize;
    Vertex input = LoadVertex(byteOffset, vertexBuffer, meshBuffer.vertexFlags);
    
    float4 pos = float4(input.position.xyz, 1.0f);
    
    PSInput output;
    float4 worldPosition = mul(pos, objectBuffer.model);

    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    
    ConstantBuffer<MaterialInfo> materialInfo = ResourceDescriptorHeap[meshBuffer.materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
    
    if (materialFlags & MATERIAL_TEXTURED) {
        output.texcoord = input.texcoord;
    }
    
#if defined(PSO_SHADOW)
    StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[perFrameBuffer.lightBufferIndex];
    LightInfo light = lights[currentLightID];
    matrix lightMatrix;
    switch(light.type) {
        case 0: { // Point light
            StructuredBuffer<unsigned int> pointLightCubemapIndicesBuffer = ResourceDescriptorHeap[perFrameBuffer.pointLightCubemapBufferIndex];
            uint lightCameraIndex = pointLightCubemapIndicesBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            break;
        }
        case 1: { // Spot light
            StructuredBuffer<unsigned int> spotLightMatrixIndexBuffer = ResourceDescriptorHeap[perFrameBuffer.spotLightMatrixBufferIndex];
            uint lightCameraIndex = spotLightMatrixIndexBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            break;
        }
        case 2: { // Directional light
            StructuredBuffer<unsigned int> directionalLightCascadeIndicesBuffer = ResourceDescriptorHeap[perFrameBuffer.directionalLightCascadeBufferIndex];
            uint lightCameraIndex = directionalLightCascadeIndicesBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            break;
        }
    }
    output.position = mul(worldPosition, lightMatrix);
    return output;
#endif // SHADOW
    
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    
    output.positionWorldSpace = worldPosition;
    float4 viewPosition = mul(worldPosition, mainCamera.view);
    output.positionViewSpace = viewPosition;
    output.position = mul(viewPosition, mainCamera.projection);
        
    uint vertexFlags = meshBuffer.vertexFlags;
    if (vertexFlags & VERTEX_SKINNED) {
        output.normalWorldSpace = normalize(input.normal);
    }
    else {
        StructuredBuffer<float4x4> normalMatrixBuffer = ResourceDescriptorHeap[normalMatrixBufferDescriptorIndex];
        float3x3 normalMatrix = (float3x3) normalMatrixBuffer[objectBuffer.normalMatrixBufferIndex];
        output.normalWorldSpace = normalize(mul(input.normal, normalMatrix));
    }

    output.color = input.color;

    output.meshletIndex = 0; // Unused for vertex shader
    
    output.normalModelSpace = input.normal;
    
    return output;
}

struct PrePassPSOutput
{
    float4 signedOctEncodedNormal;
    float4 albedo;
    float2 metallicRoughness;
};

PrePassPSOutput PrepassPSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    
    MaterialInputs fragmentInfo;
    GetMaterialInfoForFragment(input, fragmentInfo);
    
#if defined(PSO_DOUBLE_SIDED)
    if (!isFrontFace) {
        fragmentInfo.normalWS = -fragmentInfo.normalWS;
    }
#endif    

    float3 outNorm = SignedOctEncode(fragmentInfo.normalWS);
    
    PrePassPSOutput output;
    output.signedOctEncodedNormal = float4(0, outNorm.x, outNorm.y, outNorm.z);
    output.albedo = float4(fragmentInfo.albedo.xyz, fragmentInfo.ambientOcclusion);
    output.metallicRoughness = float2(fragmentInfo.metallic, fragmentInfo.roughness);
    return output;
}

#if defined(PSO_SHADOW)
void
#else
[earlydepthstencil]
float4 
#endif
PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{

    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    uint meshBufferIndex = perMeshBufferIndex;
    PerMeshBuffer meshBuffer = perMeshBuffer[meshBufferIndex];
    ConstantBuffer<MaterialInfo> materialInfo = ResourceDescriptorHeap[meshBuffer.materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
#if defined(PSO_SHADOW)
#if !defined(PSO_ALPHA_TEST) && !defined(PSO_BLEND)
        return;
#endif // DOUBLE_SIDED
    if (materialFlags & MATERIAL_BASE_COLOR_TEXTURE) {
        Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[materialInfo.baseColorTextureIndex];
        SamplerState baseColorSamplerState = SamplerDescriptorHeap[materialInfo.baseColorSamplerIndex];
        float2 uv = input.texcoord;
        float4 baseColor = baseColorTexture.Sample(baseColorSamplerState, uv);
        if (baseColor.a*materialInfo.baseColorFactor.a < 0.5){
            discard;
        }
    }
    if (materialInfo.baseColorFactor.a < 0.5){
        discard;
    }
#endif // PSO_SHADOW
#if !defined(PSO_SHADOW)

    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    float3 viewDir = normalize(mainCamera.positionWorldSpace.xyz - input.positionWorldSpace.xyz);
    
    FragmentInfo fragmentInfo;
    GetFragmentInfoScreenSpace(input.position.xy, viewDir, enableGTAO, fragmentInfo);

    LightingOutput lightingOutput = lightFragment(fragmentInfo, mainCamera, input, perFrameBuffer.activeEnvironmentIndex, perFrameBuffer.environmentBufferDescriptorIndex, isFrontFace);
    
    float3 lighting;
    switch (perFrameBuffer.tonemapType)
    {
        case TONEMAP_REINHARD_JODIE:
            lighting = reinhardJodie(lightingOutput.lighting);
            break;
        case TONEMAP_KRONOS_PBR_NEUTRAL:
            lighting = toneMap_KhronosPbrNeutral(lightingOutput.lighting);
            break;
        case TONEMAP_ACES_HILL:
            lighting = toneMapACES_Hill(lightingOutput.lighting);
            break;
        default:
            lighting = lightingOutput.lighting;
    }
    lighting = LinearToSRGB(lighting);
    
    switch (perFrameBuffer.outputType) {
        case OUTPUT_COLOR:
            return float4(lighting, 1.0);
        case OUTPUT_NORMAL: // Normal
            return float4(fragmentInfo.normalWS * 0.5 + 0.5, 1.0);
        case OUTPUT_ALBEDO:
            return float4(fragmentInfo.albedo.rgb, 1.0);
        case OUTPUT_METALLIC:
            return float4(fragmentInfo.metallic, fragmentInfo.metallic, fragmentInfo.metallic, 1.0);
        case OUTPUT_ROUGHNESS:
            return float4(fragmentInfo.roughness, fragmentInfo.roughness, fragmentInfo.roughness, 1.0);
        case OUTPUT_EMISSIVE:{
                if (materialInfo.materialFlags & MATERIAL_EMISSIVE_TEXTURE) {
                    float3 srgbEmissive = LinearToSRGB(fragmentInfo.emissive);
                    return float4(srgbEmissive, 1.0);
                } else {
                    return float4(materialInfo.emissiveFactor.rgb, 1.0);
                }
            }
        case OUTPUT_AO:
            return float4(fragmentInfo.diffuseAmbientOcclusion, fragmentInfo.diffuseAmbientOcclusion, fragmentInfo.diffuseAmbientOcclusion, 1.0);
        case OUTPUT_DEPTH:{
                float depth = abs(input.positionViewSpace.z)*0.1;
                return float4(depth, depth, depth, 1.0);
            }
#if defined(PSO_IMAGE_BASED_LIGHTING)
        case OUTPUT_DIFFUSE_IBL:
            return float4(lightingOutput.diffuseIBL.rgb, 1.0);
        case OUTPUT_SPECULAR_IBL:
            return float4(lightingOutput.specularIBL.rgb, 1.0);
#endif // IMAGE_BASED_LIGHTING
        case OUTPUT_MESHLETS:{
                return lightUints(input.meshletIndex, fragmentInfo.normalWS, viewDir);
            }
        case OUTPUT_MODEL_NORMALS:{
                return float4(input.normalModelSpace * 0.5 + 0.5, 1.0);
            }
//        case OUTPUT_LIGHT_CLUSTER_ID:{
//                return lightUints(lightingOutput.clusterID, lightingOutput.normalWS, lightingOutput.viewDir);
//            }
//        case OUTPUT_LIGHT_CLUSTER_LIGHT_COUNT:{
//                return lightUints(lightingOutput.clusterLightCount, lightingOutput.normalWS, lightingOutput.viewDir);
//            }
        default:
            return float4(1.0, 0.0, 0.0, 1.0);
    }
#endif // PSO_SHADOW
}