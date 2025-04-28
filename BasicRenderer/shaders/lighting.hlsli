#ifndef __LIGHTING_HLSLI__
#define __LIGHTING_HLSLI__

#include "vertex.hlsli"
#include "structs.hlsli"
#include "materialFlags.hlsli"
#include "parallax.hlsli"
#include "cbuffers.hlsli"
#include "PBR.hlsli"
#include "gammaCorrection.hlsli"
#include "shadows.hlsli"
#include "constants.hlsli"
#include "IBL.hlsli"

struct LightFragmentData {
    uint lightType;
    float3 lightPos;
    float3 lightColor;
    float3 lightToFrag;
    float attenuation;
    float distance;
    float spotAttenuation;
};

struct LightingParameters {
    float3 fragPos;
    float3 viewDir;
    float3 normal;
    float2 uv;
    float3 albedo;
    float metallic;
    float roughness;
    float3 F0;
};

struct LightingOutput { // Lighting + debug info
    float3 lighting;
#if defined(PSO_IMAGE_BASED_LIGHTING)
    float3 diffuseIBL;
#endif // IMAGE_BASED_LIGHTING
};

// Models spotlight falloff with linear interpolation between inner and outer cone angles
float spotAttenuation(float3 pointToLight, float3 lightDirection, float outerConeCos, float innerConeCos) {
    float cos = dot(normalize(lightDirection), normalize(-pointToLight));
    if (cos > outerConeCos) {
        if (cos < innerConeCos) {
            return smoothstep(outerConeCos, innerConeCos, cos);
        }
        return 1.0;
    }
    return 0.0;
}

LightFragmentData getLightParametersForFragment(LightInfo light, float3 fragPos) {
    LightFragmentData result;
    result.lightType = light.type;
    result.lightPos = light.posWorldSpace.xyz;
    result.lightColor = light.color.xyz;
    
    switch (light.type) {
        case 2:{
                result.lightToFrag = -light.dirWorldSpace.xyz;
                result.attenuation = 1.0;
                break;
            }
        default:{
                float constantAttenuation = light.attenuation.x;
                float linearAttenuation = light.attenuation.y;
                float quadraticAttenuation = light.attenuation.z;
                result.lightToFrag = normalize(light.posWorldSpace.xyz - fragPos);
                result.distance = length(light.posWorldSpace.xyz - fragPos);
                result.attenuation = 1.0 / ((constantAttenuation + linearAttenuation * result.distance + quadraticAttenuation * result.distance * result.distance) + 0.0001); //+0.0001 fudge-factor to prevent division by 0;
                break;
            }
    }
    
    if (light.type == 1) {
        result.spotAttenuation = spotAttenuation(result.lightToFrag, light.dirWorldSpace.xyz, light.outerConeAngle, light.innerConeAngle);
    }
    else {
        result.spotAttenuation = 1.0;
    }
    return result;
}

uint3 ComputeClusterID(float4 svPos, float viewDepth,
                         ConstantBuffer<PerFrameBuffer> perFrame, Camera mainCamera) {

    float2 tileSize = float2(perFrame.screenResX, perFrame.screenResY) / float2(perFrame.lightClusterGridSizeX, perFrame.lightClusterGridSizeY);
    uint2 tile = uint2(svPos.xy / tileSize);
    
    // Z slice piecewise
    float z = abs(viewDepth);
    uint totalZ = perFrame.lightClusterGridSizeZ;
    uint nearSlices = perFrame.nearClusterCount;
    float zSplit = perFrame.clusterZSplitDepth;
    float zNear = mainCamera.zNear;
    float zFar = mainCamera.zFar;
    uint sliceZ;

    if (z < zSplit) {
        // uniform up close
        float t = (z - zNear) / (zSplit - zNear);
        sliceZ = uint(t * nearSlices);
    }
    else {
        // logarithmic beyond zSplit
        float logStart = log(zSplit / zNear);
        float logEnd = log(zFar / zNear);
        float logZ = log(z / zNear);
        float u = (logZ - logStart) / (logEnd - logStart);
        sliceZ = nearSlices + uint(u * (totalZ - nearSlices));
    }
    
    return uint3(tile.x, tile.y, sliceZ);
}

LightingOutput lightFragment(FragmentInfo fragmentInfo, Camera mainCamera, PSInput input, uint activeEnvironmentIndex, uint environmentBufferDescriptorIndex, bool isFrontFace) {
    
    float3 indirect = evaluateIBL(
                                fragmentInfo.normalWS, 
                                fragmentInfo.normalWS, 
                                fragmentInfo.diffuseColor, 
                                fragmentInfo.diffuseAmbientOcclusion, 
                                fragmentInfo.DFG, 
                                fragmentInfo.F0, 
                                fragmentInfo.reflectedWS, 
                                fragmentInfo.roughness, 
                                activeEnvironmentIndex, 
                                environmentBufferDescriptorIndex);
    
    LightingOutput output;
    output.lighting = indirect;
    
#if defined(PSO_IMAGE_BASED_LIGHTING)
    output.diffuseIBL = indirect;
#endif // IMAGE_BASED_LIGHTING
    return output;
}

#endif // __LIGHTING_HLSLI__