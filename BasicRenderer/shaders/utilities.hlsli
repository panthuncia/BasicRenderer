#ifndef __UTILITY_HLSL__
#define __UTILITY_HLSL__
#include "structs.hlsli"
#include "cbuffers.hlsli"
#include "vertex.hlsli"
#include "materialFlags.hlsli"
#include "parallax.hlsli"
#include "gammaCorrection.hlsli"

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

struct MaterialLightingValues
{
    float3 albedo;
    float3 normalWS;
    float metallic;
    float roughness;
    float opacity;
};

void GetMaterialInfoForFragment(in const PSInput input, out MaterialLightingValues ret)
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
    
    ret.albedo = baseColor.rgb;
    ret.normalWS = normalWS;
    ret.metallic = metallic;
    ret.roughness = roughness;
    ret.opacity = baseColor.a;
}

void GetFragmentInfoScreenSpace(in uint2 pixelCoordinates, in bool enableGTAO, out FragmentInfo ret) {
    // Gather textures
    Texture2D<float4> normalsTexture = ResourceDescriptorHeap[normalsTextureDescriptorIndex];
    
    // Load values
    float4 encodedNormal = normalsTexture[pixelCoordinates];
    ret.normalWS = SignedOctDecode(encodedNormal.yzw);
    
    if (enableGTAO) {
        Texture2D<uint> aoTexture = ResourceDescriptorHeap[aoTextureDescriptorIndex];
        ret.ambientOcclusion = float(aoTexture[pixelCoordinates].x) / 255.0;
    }
    else {
        ret.ambientOcclusion = 1.0;
    }
    
    Texture2D<float4> albedoTexture = ResourceDescriptorHeap[albedoTextureDescriptorIndex];
    float3 baseColor = albedoTexture[pixelCoordinates].xyz;
    
    Texture2D<float2> metallicRoughnessTexture = ResourceDescriptorHeap[metallicRoughnessTextureDescriptorIndex];
    float2 metallicRoughness = metallicRoughnessTexture[pixelCoordinates];
    
    ret.metallic = metallicRoughness.x;
    ret.roughness = metallicRoughness.y;
    ret.diffuseColor = computeDiffuseColor(baseColor, ret.metallic);
    ret.alpha = 1.0; // Opaque objects
}

void GetFragmentInfoDirectTransparent(in PSInput input, out FragmentInfo ret)
{
    
    MaterialLightingValues materialInfo;
    GetMaterialInfoForFragment(input, materialInfo);
    
    ret.metallic = materialInfo.metallic;
    ret.roughness = materialInfo.roughness;
    ret.diffuseColor = computeDiffuseColor(materialInfo.albedo, ret.metallic);
    ret.alpha = materialInfo.opacity;
    ret.normalWS = materialInfo.normalWS;
    ret.ambientOcclusion = 1.0; // Screen-space AO not applied to transparent objects
}

#endif // __UTILITY_HLSL__