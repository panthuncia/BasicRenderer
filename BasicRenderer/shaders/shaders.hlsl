#include "vertex.hlsli"
#include "utilities.hlsli"
#include "cbuffers.hlsli"
#include "structs.hlsli"
#include "materialflags.hlsli"
#include "lighting.hlsli"
#include "tonemapping.hlsli"
#include "gammaCorrection.hlsli"

// Do enums exist in HLSL?
// The internet disagrees.
// My compiler doesn't like them though
#define OUTPUT_COLOR 0
#define OUTPUT_NORMAL 1
#define OUTPUT_ALBEDO 2
#define OUTPUT_METALLIC 3
#define OUTPUT_ROUGHNESS 4
#define OUTPUT_EMISSIVE 5
#define OUTPUT_AO 6
#define OUTPUT_DEPTH 7
#define OUTPUT_METAL_BRDF_IBL 8
#define OUTPUT_DIELECTRIC_BRDF_IBL 9
#define OUTPUT_SPECULAR_IBL 10
#define OUTPUT_METAL_FRESNEL_IBL 11
#define OUTPUT_DIELECTRIC_FRESNEL_IBL 12
#define OUTPUT_MESHLETS 13

PSInput VSMain(uint vertexID : SV_VertexID) {
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[vertexBufferDescriptorIndex];
    
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[perObjectBufferDescriptorIndex];
    PerObjectBuffer objectBuffer = perObjectBuffer[perObjectBufferIndex];
    
    uint byteOffset = meshBuffer.vertexBufferOffset + vertexID * meshBuffer.vertexByteSize;
    Vertex input = LoadVertex(byteOffset, vertexBuffer, meshBuffer.vertexFlags);
    
    float4 pos = float4(input.position.xyz, 1.0f);
    
    float3x3 normalMatrixSkinnedIfNecessary = (float3x3)objectBuffer.normalMatrix;
    
    if (meshBuffer.vertexFlags & VERTEX_SKINNED) {
        StructuredBuffer<float4> boneTransformsBuffer = ResourceDescriptorHeap[objectBuffer.boneTransformBufferIndex];
        StructuredBuffer<float4> inverseBindMatricesBuffer = ResourceDescriptorHeap[objectBuffer.inverseBindMatricesBufferIndex];
    
        matrix bone1 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.x);
        matrix bone2 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.y);
        matrix bone3 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.z);
        matrix bone4 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.w);
    
        matrix bindMatrix1 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.x);
        matrix bindMatrix2 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.y);
        matrix bindMatrix3 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.z);
        matrix bindMatrix4 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.w);

        matrix skinMatrix = input.weights.x * mul(bindMatrix1, bone1) +
                        input.weights.y * mul(bindMatrix2, bone2) +
                        input.weights.z * mul(bindMatrix3, bone3) +
                        input.weights.w * mul(bindMatrix4, bone4);
    
        pos = mul(pos, skinMatrix);
        normalMatrixSkinnedIfNecessary = mul(normalMatrixSkinnedIfNecessary, (float3x3) skinMatrix);
    }
    
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
    
    output.normalWorldSpace = normalize(mul(input.normal, normalMatrixSkinnedIfNecessary));
    
    if (materialFlags & MATERIAL_NORMAL_MAP || materialFlags & MATERIAL_PARALLAX) {
        output.TBN_T = normalize(mul(input.tangent, normalMatrixSkinnedIfNecessary));
        output.TBN_B = normalize(mul(input.bitangent, normalMatrixSkinnedIfNecessary));
        output.TBN_N = normalize(mul(input.normal, normalMatrixSkinnedIfNecessary));
    }

    output.color = input.color;

    output.meshletIndex = 0; // Unused for vertex shader
    return output;
}

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {

    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[perMeshBufferDescriptorIndex];
    uint meshBufferIndex = perMeshBufferIndex;
    PerMeshBuffer meshBuffer = perMeshBuffer[meshBufferIndex];
    ConstantBuffer<MaterialInfo> materialInfo = ResourceDescriptorHeap[meshBuffer.materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
#if defined(PSO_SHADOW) // Alpha tested shadows
    #if !defined(PSO_ALPHA_TEST) && !defined(PSO_BLEND)
        return float4(0, 0, 0, 0);
    #endif // DOUBLE_SIDED
    if (materialFlags & MATERIAL_BASE_COLOR_TEXTURE) {
        Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[materialInfo.baseColorTextureIndex];
        SamplerState baseColorSamplerState = SamplerDescriptorHeap[materialInfo.baseColorSamplerIndex];
        float2 uv = input.texcoord;
        float4 baseColor = baseColorTexture.Sample(baseColorSamplerState, uv);
        if (baseColor.a*materialInfo.baseColorFactor.a < 0.1){
            discard;
        }
    }
    if (materialInfo.baseColorFactor.a < 0.1){
        discard;
    }
    return float4(0, 0, 0, 0);
#endif // SHADOW

    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];

    LightingOutput lightingOutput = lightFragment(mainCamera, input, materialInfo, meshBuffer, perFrameBuffer, isFrontFace);
    
    // Reinhard tonemapping
    float3 lighting = reinhardJodie(lightingOutput.lighting);
    //lighting = toneMap_KhronosPbrNeutral(lighting);
    //lighting = toneMapACES_Hill(lighting);
    if (materialInfo.materialFlags & MATERIAL_PBR) {
    // Gamma correction
        lighting = LinearToSRGB(lighting);
    }
        
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
                return lightMeshlets(input.meshletIndex, lightingOutput.normalWS, lightingOutput.viewDir);
            }
        default:
            return float4(1.0, 0.0, 0.0, 1.0);
    }
}