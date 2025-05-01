#ifndef __UTILITY_HLSL__
#define __UTILITY_HLSL__
#include "structs.hlsli"
#include "cbuffers.hlsli"
#include "vertex.hlsli"
#include "materialFlags.hlsli"
#include "parallax.hlsli"
#include "gammaCorrection.hlsli"
#include "constants.hlsli"

// Basic blinn-phong for uint visualization
float4 lightUints(uint meshletIndex, float3 normal, float3 viewDir) {
    float ambientIntensity = 0.3;
    float3 lightColor = float3(1, 1, 1);
    float3 lightDir = -normalize(float3(1, -1, 1));

    float3 diffuseColor = float3(
            float(meshletIndex & 1),
            float(meshletIndex & 3) / 4,
            float(meshletIndex & 7) / 8);
   float shininess = 16.0;
    
    float cosAngle = saturate(dot(normal, lightDir));
    float3 halfAngle = normalize(lightDir + viewDir);

    float blinnTerm = saturate(dot(normal, halfAngle));
    blinnTerm = cosAngle != 0.0 ? blinnTerm : 0.0;
    blinnTerm = pow(blinnTerm, shininess);

    float3 finalColor = (cosAngle + blinnTerm + ambientIntensity) * diffuseColor;

    return float4(finalColor, 1);
}

// https://johnwhite3d.blogspot.com/2017/10/signed-octahedron-normal-encoding.html
#define FLT_MAX 3.402823466e+38f
float3 SignedOctEncode(float3 n) {
    float3 OutN;

    n /= (abs(n.x) + abs(n.y) + abs(n.z));

    OutN.y = n.y * 0.5 + 0.5;
    OutN.x = n.x * 0.5 + OutN.y;
    OutN.y = n.x * -0.5 + OutN.y;

    OutN.z = saturate(n.z * FLT_MAX);
    return OutN;
}

float3 SignedOctDecode(float3 n) {
    float3 OutN;

    OutN.x = (n.x - n.y);
    OutN.y = (n.x + n.y) - 1.0;
    OutN.z = n.z * 2.0 - 1.0;
    OutN.z = OutN.z * (1.0 - abs(OutN.x) - abs(OutN.y));
 
    OutN = normalize(OutN);
    return OutN;
}

float3 computeDiffuseColor(const float3 baseColor, float metallic){
    return baseColor.rgb * (1.0 - metallic);
}

//http://www.thetenthplanet.de/archives/1180
float3x3 cotangent_frame(float3 N, float3 p, float2 uv)
{
    // get edge vectors of the pixel triangle 
    float3 dp1 = ddx(p);
    float3 dp2 = ddy(p);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    // solve the linear system 
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    // construct a scale-invariant frame 
    float invmax = rsqrt(max(dot(T, T), dot(B, B)));
    return float3x3(T * invmax, B * invmax, N);
}

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

void GetMaterialInfoForFragment(in const PSInput input, out MaterialInputs ret)
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
    
    float4 baseColor = materialInfo.baseColorFactor;
    
    if (materialFlags & MATERIAL_BASE_COLOR_TEXTURE)
    {
        Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[materialInfo.baseColorTextureIndex];
        SamplerState baseColorSamplerState = SamplerDescriptorHeap[materialInfo.baseColorSamplerIndex];
        float4 sampledColor = baseColorTexture.Sample(baseColorSamplerState, uv);
#if defined(PSO_ALPHA_TEST) || defined (PSO_BLEND)
        if (baseColor.a * sampledColor.a < materialInfo.alphaCutoff){
            discard;
        }
#endif // PSO_ALPHA_TEST || PSO_BLEND
        sampledColor.rgb = SRGBToLinear(sampledColor.rgb);
        baseColor = baseColor * sampledColor;
    }

    // Metallic-roughness
    float metallic = 0.0;
    float roughness = 0.0;
    
    if (materialFlags & MATERIAL_PBR)
    {
        if (materialFlags & MATERIAL_PBR_MAPS)
        {
            Texture2D<float4> metallicTexture = ResourceDescriptorHeap[materialInfo.metallicTextureIndex];
            SamplerState metallicSamplerState = SamplerDescriptorHeap[materialInfo.metallicSamplerIndex];
            Texture2D<float4> roughnessTexture = ResourceDescriptorHeap[materialInfo.roughnessTextureIndex];
            SamplerState roughnessSamplerState = SamplerDescriptorHeap[materialInfo.roughnessSamplerIndex];
            metallic = metallicTexture.Sample(metallicSamplerState, uv).b * materialInfo.metallicFactor;
            roughness = roughnessTexture.Sample(roughnessSamplerState, uv).g * materialInfo.roughnessFactor;
        }
        else
        {
            metallic = materialInfo.metallicFactor;
            roughness = materialInfo.roughnessFactor;
        }
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
    
    float ao = 1.0;
    if (materialInfo.materialFlags & MATERIAL_AO_TEXTURE)
    {
        Texture2D<float4> aoTexture = ResourceDescriptorHeap[materialInfo.aoMapIndex];
        SamplerState aoSamplerState = SamplerDescriptorHeap[materialInfo.aoSamplerIndex];
        ao = aoTexture.Sample(aoSamplerState, uv).r;
    }
    
    float3 emissive = float3(0.0, 0.0, 0.0);
    if (materialInfo.materialFlags & MATERIAL_EMISSIVE_TEXTURE)
    {
        Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[materialInfo.emissiveTextureIndex];
        SamplerState emissiveSamplerState = SamplerDescriptorHeap[materialInfo.emissiveSamplerIndex];
        emissive = SRGBToLinear(emissiveTexture.Sample(emissiveSamplerState, uv).rgb) * materialInfo.emissiveFactor.rgb;
    }
    else
    {
        emissive = materialInfo.emissiveFactor.rgb;
    }
    
    ret.albedo = baseColor.rgb;
    ret.normalWS = normalWS;
    ret.emissive = emissive;
    ret.metallic = metallic;
    ret.roughness = roughness;
    ret.opacity = baseColor.a;
    ret.ambientOcclusion = ao;
}

float PerceptualRoughnessToRoughness(float perceptualRoughness)
{
    return perceptualRoughness * perceptualRoughness;
}

float3 computeF0(const float4 baseColor, float metallic, float reflectance)
{
    return baseColor.rgb * metallic + (reflectance * (1.0 - metallic));
}

float computeDielectricF0(float reflectance)
{
    return 0.16 * reflectance * reflectance;
}

void GetFragmentInfoScreenSpace(in uint2 pixelCoordinates, in float3 viewWS, in float3 fragPosViewSpace, in float3 fragPosWorldSpace, in bool enableGTAO, out FragmentInfo ret) {
    ret.pixelCoords = pixelCoordinates;
    ret.fragPosViewSpace = fragPosViewSpace;
    ret.fragPosWorldSpace = fragPosWorldSpace;
    
    // Gather textures
    Texture2D<float4> normalsTexture = ResourceDescriptorHeap[normalsTextureDescriptorIndex];
    
    // Load values
    float4 encodedNormal = normalsTexture[pixelCoordinates];
    ret.normalWS = SignedOctDecode(encodedNormal.yzw);
    
    Texture2D<float4> albedoTexture = ResourceDescriptorHeap[albedoTextureDescriptorIndex];
    float4 baseColorSample = albedoTexture[pixelCoordinates];
    ret.albedo = baseColorSample.xyz;
    
    Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[emissiveTextureDescriptorIndex];
    float4 emissive = emissiveTexture[pixelCoordinates];
    ret.emissive = emissive.xyz;
    
    if (enableGTAO)
    {
        Texture2D<uint> aoTexture = ResourceDescriptorHeap[aoTextureDescriptorIndex];
        ret.diffuseAmbientOcclusion = min(baseColorSample.w, float(aoTexture[pixelCoordinates].x) / 255.0);
    }
    else
    {
        ret.diffuseAmbientOcclusion = baseColorSample.w; // AO stored in alpha channel
    }
    
    Texture2D<float2> metallicRoughnessTexture = ResourceDescriptorHeap[metallicRoughnessTextureDescriptorIndex];
    float2 metallicRoughness = metallicRoughnessTexture[pixelCoordinates];
    
    float perceptualRoughness = metallicRoughness.y;
    ret.perceptualRoughnessUnclamped = perceptualRoughness;
    // Clamp the roughness to a minimum value to avoid divisions by 0 during lighting
    ret.perceptualRoughness = clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
    // Remaps the roughness to a perceptually linear roughness (roughness^2)
    ret.roughness = PerceptualRoughnessToRoughness(ret.perceptualRoughness);
    ret.roughnessUnclamped = PerceptualRoughnessToRoughness(ret.perceptualRoughnessUnclamped);
    
    ret.viewWS = viewWS;
    //ret.NdotV = dot(ret.normalWS, viewWS);
    ret.NdotV = dot(ret.normalWS, ret.viewWS);
    ret.normalWS = normalize(ret.normalWS + max(0, -ret.NdotV + MIN_N_DOT_V) * ret.viewWS);
    ret.NdotV = max(MIN_N_DOT_V, ret.NdotV);
    ret.reflectedWS = reflect(-ret.viewWS, ret.normalWS);
    
    //ret.DFG = prefilteredDFG(ret.perceptualRoughness, ret.NdotV);
    
    ret.metallic = metallicRoughness.x;
    ret.diffuseColor = computeDiffuseColor(baseColorSample.xyz, ret.metallic);
    ret.alpha = 1.0; // Opaque objects
    
    ret.reflectance = 0.35; // This is a default value for the reflectance of dielectrics, similar to setting an F0 directly. Ideally, each material should have its own reflectance value.
    // Assumes an interface from air to an IOR of 1.5 for dielectrics
    ret.dielectricF0 = computeDielectricF0(ret.reflectance);
    ret.F0 = computeF0(float4(baseColorSample.xyz, 1.0), ret.metallic, ret.dielectricF0); // base albedo, not the diffuse color
    ret.dielectricF0 *= (1.0 - ret.metallic);
}

void GetFragmentInfoDirect(in PSInput input, in float3 viewWS, bool enableGTAO, bool transparent, bool isFrontFace, out FragmentInfo ret)
{
    ret.pixelCoords = input.position.xy;
    ret.fragPosViewSpace = input.positionViewSpace.xyz;
    ret.fragPosWorldSpace = input.positionWorldSpace.xyz;
    
    MaterialInputs materialInfo;
    GetMaterialInfoForFragment(input, materialInfo);
    
    ret.metallic = materialInfo.metallic;
    float perceptualRoughness = materialInfo.roughness;
    ret.perceptualRoughnessUnclamped = perceptualRoughness;
        // Clamp the roughness to a minimum value to avoid divisions by 0 during lighting
    ret.perceptualRoughness = clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
        // Remaps the roughness to a perceptually linear roughness (roughness^2)
    ret.roughness = PerceptualRoughnessToRoughness(ret.perceptualRoughness);
    ret.roughnessUnclamped = PerceptualRoughnessToRoughness(ret.perceptualRoughnessUnclamped);

    ret.normalWS = materialInfo.normalWS;
#if defined (PSO_DOUBLE_SIDED)
        if (!isFrontFace) {
            ret.normalWS = -ret.normalWS;
        }
#endif
    
    ret.viewWS = viewWS;
    //ret.NdotV = dot(ret.normalWS, viewWS);
    ret.NdotV = dot(ret.normalWS, ret.viewWS);
    ret.normalWS = normalize(ret.normalWS + max(0, -ret.NdotV + MIN_N_DOT_V) * ret.viewWS);
    ret.NdotV = max(MIN_N_DOT_V, ret.NdotV);
    
    ret.reflectedWS = reflect(-ret.viewWS, ret.normalWS);
    
    //ret.DFG = prefilteredDFG(ret.perceptualRoughness, ret.NdotV);
    
    ret.diffuseColor = computeDiffuseColor(materialInfo.albedo, ret.metallic);
    ret.albedo = materialInfo.albedo;
    if (transparent) {
        ret.alpha = materialInfo.opacity;
        ret.diffuseAmbientOcclusion = materialInfo.ambientOcclusion; // Screen-space AO not applied to transparent objects
    } else {
        ret.alpha = 1.0; // Opaque objects
        if (enableGTAO) {
            float2 pixelCoordinates = input.position.xy;
            Texture2D<uint> aoTexture = ResourceDescriptorHeap[aoTextureDescriptorIndex];
            ret.diffuseAmbientOcclusion = min(materialInfo.ambientOcclusion, float(aoTexture[pixelCoordinates].x) / 255.0);
        } else {
            ret.diffuseAmbientOcclusion = materialInfo.ambientOcclusion;
        }
    }
    
    ret.reflectance = 0.35; // This is a default value for the reflectance of dielectrics, similar to setting an F0 directly. Ideally, each material should have its own reflectance value.
    // Assumes an interface from air to an IOR of 1.5 for dielectrics
    ret.dielectricF0 = computeDielectricF0(ret.reflectance);
    ret.F0 = computeF0(float4(materialInfo.albedo.xyz, 1.0), ret.metallic, ret.dielectricF0); // base albedo, not the diffuse color
    ret.dielectricF0 *= (1.0 - ret.metallic);
    
    ret.emissive = materialInfo.emissive; // TODO
}

float unprojectDepth(float depth, float near, float far)
{
    return near * far / (far - depth * (far - near));
}

#endif // __UTILITY_HLSL__