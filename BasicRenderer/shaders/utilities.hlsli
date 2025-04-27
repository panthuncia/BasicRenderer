#ifndef __UTILITY_HLSL__
#define __UTILITY_HLSL__
#include "structs.hlsli"
#include "cbuffers.hlsli"
#include "vertex.hlsli"

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

FragmentInfo GetFragmentInfoScreenSpace(in uint2 pixelCoordinates, in bool enableGTAO) {
    FragmentInfo info;
    // Gather textures
    Texture2D<float4> normalsTexture = ResourceDescriptorHeap[normalsTextureDescriptorIndex];
    
    // Load values
    float4 encodedNormal = normalsTexture[pixelCoordinates];
    info.normalWS = SignedOctDecode(encodedNormal.yzw);
    
    if (enableGTAO) {
        Texture2D<uint> aoTexture = ResourceDescriptorHeap[aoTextureDescriptorIndex];
        info.ambientOcclusion = float(aoTexture[pixelCoordinates].x) / 255.0;
    }
    else {
        info.ambientOcclusion = 1.0;
    }
    
    Texture2D<float4> albedoTexture = ResourceDescriptorHeap[albedoTextureDescriptorIndex];
    info.diffuseColor = albedoTexture[pixelCoordinates].xyz;
    
    return info;
}

FragmentInfo GetFragmentInfoDirectTransparent(in PSInput input) {
    FragmentInfo info;
    
    info.normalWS = input.normalWorldSpace;
    info.ambientOcclusion = 1.0; // Screen-space AO not applied to transparent objects
    return info;
}

#endif // __UTILITY_HLSL__