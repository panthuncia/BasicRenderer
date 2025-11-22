#include "include/vertex.hlsli"
#include "include/utilities.hlsli"
#include "include/cbuffers.hlsli"
#include "include/structs.hlsli"
#include "include/materialflags.hlsli"
#include "include/lighting.hlsli"
#include "include/gammaCorrection.hlsli"
#include "include/outputTypes.hlsli"
#include "include/materialFlags.hlsli"
#include "fullscreenVS.hlsli"

PSInput VSMain(uint vertexID : SV_VertexID) {
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostSkinningVertices)];
    
    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    PerMeshBuffer meshBuffer = perMeshBuffer[perMeshBufferIndex];
    StructuredBuffer<PerMeshInstanceBuffer> perMeshInstanceBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshInstanceBuffer)];
    PerMeshInstanceBuffer meshInstanceBuffer = perMeshInstanceBuffer[perMeshInstanceBufferIndex];
    
    StructuredBuffer<PerObjectBuffer> perObjectBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerObjectBuffer)];
    PerObjectBuffer objectBuffer = perObjectBuffer[perObjectBufferIndex];
            
    uint vertexFlags = meshBuffer.vertexFlags;
    
    uint postSkinningBufferOffset = meshInstanceBuffer.postSkinningVertexBufferOffset;
    
    uint prevPostSkinningBufferOffset = postSkinningBufferOffset;
    if (meshBuffer.vertexFlags & VERTEX_SKINNED)
    {
        postSkinningBufferOffset += meshBuffer.vertexByteSize * meshBuffer.numVertices * (perFrameBuffer.frameIndex % 2);
        prevPostSkinningBufferOffset += meshBuffer.vertexByteSize * meshBuffer.numVertices * ((perFrameBuffer.frameIndex + 1) % 2);
    }
    
    uint byteOffset = postSkinningBufferOffset + vertexID * meshBuffer.vertexByteSize;
    Vertex input = LoadVertex(byteOffset, vertexBuffer, meshBuffer.vertexFlags);
    
    float4 pos = float4(input.position.xyz, 1.0f);
    
    float4 prevPos;
    if (vertexFlags & VERTEX_SKINNED)
    {
        uint prevByteOffset = prevPostSkinningBufferOffset + vertexID * meshBuffer.vertexByteSize;
        prevPos = float4(LoadFloat3(prevByteOffset, vertexBuffer), 1.0);
    }
    else
    {
        prevPos = float4(input.position.xyz, 1.0f);
    }
    
    PSInput output;
    float4 worldPosition = mul(pos, objectBuffer.model);

    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    
    ConstantBuffer<MaterialInfo> materialInfo = ResourceDescriptorHeap[meshBuffer.materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
    
    if (materialFlags & MATERIAL_TEXTURED) {
        output.texcoord = input.texcoord;
    }
    
#if defined(PSO_SHADOW)
    StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Light::InfoBuffer)];
    LightInfo light = lights[currentLightID];
    matrix lightMatrix;
    matrix viewMatrix;
    switch(light.type) {
        case 0: { // Point light
            StructuredBuffer<unsigned int> pointLightCubemapIndicesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Light::PointLightCubemapBuffer)];
            uint lightCameraIndex = pointLightCubemapIndicesBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            viewMatrix = lightCamera.view;
            break;
        }
        case 1: { // Spot light
            StructuredBuffer<unsigned int> spotLightMatrixIndexBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Light::SpotLightMatrixBuffer)];
            uint lightCameraIndex = spotLightMatrixIndexBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            viewMatrix = lightCamera.view;
            break;
        }
        case 2: { // Directional light
            StructuredBuffer<unsigned int> directionalLightCascadeIndicesBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Light::DirectionalLightCascadeBuffer)];
            uint lightCameraIndex = directionalLightCascadeIndicesBuffer[lightViewIndex];
            Camera lightCamera = cameras[lightCameraIndex];
            lightMatrix = lightCamera.viewProjection;
            viewMatrix = lightCamera.view;
            break;
        }
    }
    output.position = mul(worldPosition, lightMatrix);
    output.positionViewSpace = mul(worldPosition, viewMatrix);
    return output;
#endif // SHADOW
    
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    
    output.positionWorldSpace = worldPosition;
    float4 viewPosition = mul(worldPosition, mainCamera.view);
    output.positionViewSpace = viewPosition;
    output.position = mul(viewPosition, mainCamera.projection);
    output.clipPosition = mul(viewPosition, mainCamera.unjitteredProjection);
        
    float4 prevPosition = mul(prevPos, objectBuffer.model);
    prevPosition = mul(prevPosition, mainCamera.prevView);
    output.prevClipPosition = mul(prevPosition, mainCamera.unjitteredProjection);
    
    if (vertexFlags & VERTEX_SKINNED) {
        output.normalWorldSpace = normalize(input.normal);
    }
    else {
        StructuredBuffer<float4x4> normalMatrixBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::NormalMatrixBuffer)];
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
    float4 normal;
    float2 motionVector;
    float linearDepth;
#if defined(PSO_DEFERRED)
    float4 albedo;
    float2 metallicRoughness;
    float4 emissive;
#endif
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

    //float3 outNorm = SignedOctEncode(fragmentInfo.normalWS);
    
    PrePassPSOutput output;
    output.normal = float4(fragmentInfo.normalWS, 1.0);
    output.linearDepth = -input.positionViewSpace.z;
    
    // Motion vector
    float3 NDCPos = (input.clipPosition / input.clipPosition.w).xyz;
    float3 PrevNDCPos = (input.prevClipPosition / input.prevClipPosition.w).xyz;
    output.motionVector = (NDCPos - PrevNDCPos).xy;
    
#if defined(PSO_DEFERRED)
    output.albedo = float4(fragmentInfo.albedo.xyz, fragmentInfo.ambientOcclusion);
    output.metallicRoughness = float2(fragmentInfo.metallic, fragmentInfo.roughness);
    output.emissive = float4(fragmentInfo.emissive.xyz, 0.0);
#endif
    return output;
}

struct VisBufferOutput
{
    uint2 visibility; // first element packed drawcall id and meshlet-local vertex index, second component per-drawcall meshlet index
};

VisBufferOutput VisibilityBufferPSMain(VisBufferPSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    VisBufferOutput output;
    // Pack draw call ID and meshlet vertex index into a single uint, max 256 vertices per meshlet, so 8 bits for vertex index and 24 bits for draw call ID
    output.visibility[0] = (perObjectBufferIndex << 8) | (input.meshletVertexIndex & 0xFF);
    output.visibility[1] = input.meshletIndex;
    return output;
}

#if defined(PSO_SHADOW)
float
#else
[earlydepthstencil]
float4 
#endif
PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{

    StructuredBuffer<PerMeshBuffer> perMeshBuffer = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PerMeshBuffer)];
    uint meshBufferIndex = perMeshBufferIndex;
    PerMeshBuffer meshBuffer = perMeshBuffer[meshBufferIndex];
    ConstantBuffer<MaterialInfo> materialInfo = ResourceDescriptorHeap[meshBuffer.materialDataIndex];
    uint materialFlags = materialInfo.materialFlags;
#if defined(PSO_SHADOW)
#if !defined(PSO_ALPHA_TEST) && !defined(PSO_BLEND)
        return -input.positionViewSpace.z;
#endif // DOUBLE_SIDED
    if (materialFlags & MATERIAL_BASE_COLOR_TEXTURE && !(materialFlags & MATERIAL_OPACITY_TEXTURE)) { // Opacity texture overrides base color alpha for shadow
        Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[materialInfo.baseColorTextureIndex];
        SamplerState baseColorSamplerState = SamplerDescriptorHeap[materialInfo.baseColorSamplerIndex];
        float2 uv = input.texcoord;
        float4 baseColor = baseColorTexture.Sample(baseColorSamplerState, uv);
        if (baseColor.a*materialInfo.baseColorFactor.a < 0.5){
            discard;
        }
    }
    if (materialFlags & MATERIAL_OPACITY_TEXTURE)
    { 
        Texture2D<float4> opacityTexture = ResourceDescriptorHeap[materialInfo.opacityTextureIndex];
        SamplerState opacitySamplerState = SamplerDescriptorHeap[materialInfo.opacitySamplerIndex];
        float2 uv = input.texcoord;
        float4 opacitySample = opacityTexture.Sample(opacitySamplerState, uv);
        float opacity = opacitySample.a;
        if (opacity < materialInfo.alphaCutoff) {
            discard;
        }
    }
    if (materialInfo.baseColorFactor.a < 0.5){
        discard;
    }
    return -input.positionViewSpace.z; // Shadow outputs linear depth
#endif // PSO_SHADOW
#if !defined(PSO_SHADOW)

    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    float3 viewDir = normalize(mainCamera.positionWorldSpace.xyz - input.positionWorldSpace.xyz);
    
    FragmentInfo fragmentInfo;
    GetFragmentInfoDirect(input, viewDir, enableGTAO, false, isFrontFace, fragmentInfo);

    LightingOutput lightingOutput = lightFragment(fragmentInfo, mainCamera, perFrameBuffer.activeEnvironmentIndex, ResourceDescriptorIndex(Builtin::Environment::InfoBuffer), isFrontFace);
    
    float3 lighting = lightingOutput.lighting;
    
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
        case OUTPUT_EMISSIVE:
            return float4(fragmentInfo.emissive.rgb, 1.0);
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

float4 PSMainDeferred(FULLSCREEN_VS_OUTPUT input) : SV_Target
{    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    
    StructuredBuffer<Camera> cameras = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::CameraBuffer)];
    Camera mainCamera = cameras[perFrameBuffer.mainCameraIndex];
    
    Texture2D<float> depthTexture = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PrimaryCamera::DepthTexture)];
    float depth = depthTexture[input.position.xy];
    
    float linearZ = unprojectDepth(depth, mainCamera.zNear, mainCamera.zFar);
    
    float2 pixel = input.position.xy;
    //float2 uv = (pixel) / float2(perFrameBuffer.screenResX, perFrameBuffer.screenResY); // [0,1] over screen
    float2 ndc = input.uv * 2.0f - 1.0f;
    
    float4 clipPos = float4(ndc, 1.0f, 1.0f);

    // unproject back into view space:
    float4 viewPosH = mul(clipPos, mainCamera.projectionInverse);

    float3 positionVS = viewPosH.xyz * linearZ;
    //positionVS.y = -positionVS.y;
    
    float4 worldPosH = mul(float4(positionVS, 1.0f), mainCamera.viewInverse);
    float3 positionWS = worldPosH.xyz;
    
    float3 viewDirWS = normalize(mainCamera.positionWorldSpace.xyz - positionWS.xyz);
    
    FragmentInfo fragmentInfo;
    GetFragmentInfoScreenSpace(input.position.xy, viewDirWS, positionVS.xyz, positionWS.xyz, enableGTAO, fragmentInfo);
        
    LightingOutput lightingOutput = lightFragment(fragmentInfo, mainCamera, perFrameBuffer.activeEnvironmentIndex, ResourceDescriptorIndex(Builtin::Environment::InfoBuffer), true);
    
    float3 lighting = lightingOutput.lighting;
    
    switch (perFrameBuffer.outputType)
    {
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
        case OUTPUT_EMISSIVE:
            return float4(fragmentInfo.emissive.rgb, 1.0);
        case OUTPUT_AO:
            return float4(fragmentInfo.diffuseAmbientOcclusion, fragmentInfo.diffuseAmbientOcclusion, fragmentInfo.diffuseAmbientOcclusion, 1.0);
        case OUTPUT_DEPTH:{
                float scaledDepth = abs(linearZ) * 0.1;
                //float scaledDepth = depth * 0.1;
                return float4(scaledDepth, scaledDepth, scaledDepth, 1.0);
            }
#if defined(PSO_IMAGE_BASED_LIGHTING)
        case OUTPUT_DIFFUSE_IBL:
            return float4(lightingOutput.diffuseIBL.rgb, 1.0);
        case OUTPUT_SPECULAR_IBL:
            return float4(lightingOutput.specularIBL.rgb, 1.0);
#endif // IMAGE_BASED_LIGHTING
        /*case OUTPUT_MESHLETS:{
                return lightUints(input.meshletIndex, fragmentInfo.normalWS, viewDir);
            }*/
        /*case OUTPUT_MODEL_NORMALS:{
                return float4(input.normalModelSpace * 0.5 + 0.5, 1.0);
            }*/
//        case OUTPUT_LIGHT_CLUSTER_ID:{
//                return lightUints(lightingOutput.clusterID, lightingOutput.normalWS, lightingOutput.viewDir);
//            }
//        case OUTPUT_LIGHT_CLUSTER_LIGHT_COUNT:{
//                return lightUints(lightingOutput.clusterLightCount, lightingOutput.normalWS, lightingOutput.viewDir);
//            }
        default:
            return float4(1.0, 0.0, 0.0, 1.0);
    }
}