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
};

PrePassPSOutput PrepassPSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    uint meshBufferIndex = perMeshBufferIndex;
    PerMeshBuffer meshBuffer = perMeshBuffer[meshBufferIndex];
    ConstantBuffer<MaterialInfo> materialInfo = ResourceDescriptorHeap[meshBuffer.materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
    
    float2 uv = input.texcoord;
    
    // Parallax UV offset
    float3x3 TBN;
    if (materialFlags & MATERIAL_NORMAL_MAP || materialFlags & MATERIAL_PARALLAX)
    {
        //TBN = float3x3(input.TBN_T, input.TBN_B, input.TBN_N);
        TBN = cotangent_frame(input.normalWorldSpace.xyz, input.positionWorldSpace.xyz, uv);
    }

    float height = 0.0;
    
    if (materialFlags & MATERIAL_PARALLAX)
    {
        ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
        StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
        Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
        float3 viewDir = normalize(mainCamera.positionWorldSpace.xyz - input.positionWorldSpace.xyz);
        Texture2D<float> parallaxTexture = ResourceDescriptorHeap[materialInfo.heightMapIndex];
        SamplerState parallaxSamplerState = SamplerDescriptorHeap[materialInfo.heightSamplerIndex];
        float3 uvh = getContactRefinementParallaxCoordsAndHeight(parallaxTexture, parallaxSamplerState, TBN, uv, viewDir, materialInfo.heightMapScale);
        uv = uvh.xy;
    }
    
    // Albedo
    
    float4 baseColor = float4(1.0, 1.0, 1.0, 1.0);
    
    if (materialFlags & MATERIAL_BASE_COLOR_TEXTURE)
    {
        Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[materialInfo.baseColorTextureIndex];
        SamplerState baseColorSamplerState = SamplerDescriptorHeap[materialInfo.baseColorSamplerIndex];
        baseColor = baseColorTexture.Sample(baseColorSamplerState, uv);
#if defined(PSO_ALPHA_TEST) || defined (PSO_BLEND)
        if (baseColor.a < materialInfo.alphaCutoff){
            discard;
        }
#endif // PSO_ALPHA_TEST || PSO_BLEND
        baseColor.rgb = SRGBToLinear(baseColor.rgb);
    }
    
    // Normal
    float3 normalWS = input.normalWorldSpace;
    if (materialFlags & MATERIAL_NORMAL_MAP)
    {
        Texture2D<float4> normalTexture = ResourceDescriptorHeap[materialInfo.normalTextureIndex];
        SamplerState normalSamplerState = SamplerDescriptorHeap[materialInfo.normalSamplerIndex];
        float3 textureNormal = normalTexture.Sample(normalSamplerState, uv).rgb;
        float3 tangentSpaceNormal = normalize(textureNormal * 2.0 - 1.0);
        if (materialFlags & MATERIAL_INVERT_NORMALS)
        {
            tangentSpaceNormal = -tangentSpaceNormal;
        }
        normalWS = normalize(mul(tangentSpaceNormal, TBN));
    }
    
    float3 outNorm = SignedOctEncode(normalWS);
    
    PrePassPSOutput output;
    output.signedOctEncodedNormal = float4(0, outNorm.x, outNorm.y, outNorm.z);
    output.albedo = baseColor;
    return output;
}

#if defined(PSO_SHADOW)
void
#else
[earlydepthstencil]
float4 
#endif
PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {

    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    uint meshBufferIndex = perMeshBufferIndex;
    PerMeshBuffer meshBuffer = perMeshBuffer[meshBufferIndex];
    ConstantBuffer<MaterialInfo> materialInfo = ResourceDescriptorHeap[meshBuffer.materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
#if defined(PSO_SHADOW) || defined(PSO_PREPASS) // Alpha tested shadows
    #if !defined(PSO_ALPHA_TEST) && !defined(PSO_BLEND) && !defined(PSO_PREPASS)
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
#if defined(PSO_PREPASS)
    float3 outNorm = normalize(input.normalWorldSpace);
    outNorm = SignedOctEncode(outNorm);
    return float4(0, outNorm.x, outNorm.y, outNorm.z);
#endif // PSO_PREPASS
#endif // PSO_SHADOW || PSO_PREPASS
#if !defined(PSO_SHADOW) && !defined(PSO_PREPASS)

    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    
    FragmentInfo fragmentInfo = GetFragmentInfoScreenSpace(input.position.xy, enableGTAO);

    LightingOutput lightingOutput = lightFragment(fragmentInfo, mainCamera, input, materialInfo, meshBuffer, perFrameBuffer, isFrontFace);
    
    // Reinhard tonemapping
    float3 lighting = reinhardJodie(lightingOutput.lighting);
    //lighting = toneMap_KhronosPbrNeutral(lighting);
    //lighting = toneMapACES_Hill(lighting);
    lighting = LinearToSRGB(lighting);
        
    float opacity = lightingOutput.baseColor.a;
    
    switch (perFrameBuffer.outputType) {
        case OUTPUT_COLOR:
            return float4(lighting, opacity);
        case OUTPUT_NORMAL: // Normal
            return float4(lightingOutput.normalWS * 0.5 + 0.5, opacity);
        case OUTPUT_ALBEDO:
            return float4(lightingOutput.baseColor.rgb, opacity);
        case OUTPUT_METALLIC:
            return float4(lightingOutput.metallic, lightingOutput.metallic, lightingOutput.metallic, opacity);
        case OUTPUT_ROUGHNESS:
            return float4(lightingOutput.roughness, lightingOutput.roughness, lightingOutput.roughness, opacity);
        case OUTPUT_EMISSIVE:{
                if (materialInfo.materialFlags & MATERIAL_EMISSIVE_TEXTURE) {
                    float3 srgbEmissive = LinearToSRGB(lightingOutput.emissive);
                    return float4(srgbEmissive, opacity);
                } else {
                    return float4(materialInfo.emissiveFactor.rgb, opacity);
                }
            }
        case OUTPUT_AO:
            return float4(lightingOutput.ao, lightingOutput.ao, lightingOutput.ao, opacity);
        case OUTPUT_DEPTH:{
                float depth = abs(input.positionViewSpace.z)*0.1;
                return float4(depth, depth, depth, opacity);
            }
#if defined(PSO_IMAGE_BASED_LIGHTING)
        case OUTPUT_METAL_BRDF_IBL:
            return float4(lightingOutput.f_metal_brdf_ibl, opacity);
        case OUTPUT_DIELECTRIC_BRDF_IBL:
            return float4(lightingOutput.f_dielectric_brdf_ibl, opacity);
        case OUTPUT_SPECULAR_IBL:
            return float4(lightingOutput.f_specular_metal, opacity);
        case OUTPUT_METAL_FRESNEL_IBL:
            return float4(lightingOutput.f_metal_fresnel_ibl, opacity);
        case OUTPUT_DIELECTRIC_FRESNEL_IBL:
            return float4(lightingOutput.f_dielectric_fresnel_ibl, opacity);
#endif // IMAGE_BASED_LIGHTING
        case OUTPUT_MESHLETS:{
                return lightUints(input.meshletIndex, lightingOutput.normalWS, lightingOutput.viewDir);
            }
        case OUTPUT_MODEL_NORMALS:{
                return float4(input.normalModelSpace * 0.5 + 0.5, opacity);
            }
        case OUTPUT_LIGHT_CLUSTER_ID:{
                return lightUints(lightingOutput.clusterID, lightingOutput.normalWS, lightingOutput.viewDir);
            }
        case OUTPUT_LIGHT_CLUSTER_LIGHT_COUNT:{
                return lightUints(lightingOutput.clusterLightCount, lightingOutput.normalWS, lightingOutput.viewDir);
            }
        default:
            return float4(1.0, 0.0, 0.0, 1.0);
    }
#endif // PSO_SHADOW
}