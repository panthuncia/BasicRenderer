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
    float4 baseColor;
    float3 normalWS;
    float metallic;
    float roughness;
    float ao;
    float3 emissive;
    float3 viewDir;
    uint clusterID;
    uint clusterLightCount;
#if defined(PSO_IMAGE_BASED_LIGHTING)
    float3 f_metal_brdf_ibl;
    float3 f_dielectric_brdf_ibl;
    float3 f_specular_metal;
    float3 f_metal_fresnel_ibl;
    float3 f_dielectric_fresnel_ibl;
#endif // IMAGE_BASED_LIGHTING
};

//http://www.thetenthplanet.de/archives/1180
float3x3 cotangent_frame(float3 N, float3 p, float2 uv) {
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
    float invmax = rsqrt( max( dot(T,T), dot(B,B) ) ); 
    return float3x3( T * invmax, B * invmax, N ); 
}

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

LightingOutput lightFragment(FragmentInfo fragmentInfo, Camera mainCamera, PSInput input, ConstantBuffer<MaterialInfo> materialInfo, PerMeshBuffer meshBuffer, ConstantBuffer<PerFrameBuffer> perFrameBuffer, bool isFrontFace) {
    
    LightingOutput output;
    output.lighting = float3(1, 0, 0);
    output.baseColor = float4(1, 0, 0, 1);
    output.normalWS = float3(0, 0, 0);
    output.metallic = 0;
    output.roughness = 0;
    output.ao = 0;
    output.emissive = float3(0, 0, 0);
    output.viewDir = float3(0, 0, 0);
    output.clusterID = 0;
    output.clusterLightCount = 0;
#if defined(PSO_IMAGE_BASED_LIGHTING)
    output.f_metal_brdf_ibl = float3(0, 0, 0);
    output.f_dielectric_brdf_ibl = float3(0, 0, 0);
    output.f_specular_metal = float3(0, 0, 0);
    output.f_metal_fresnel_ibl = float3(0, 0, 0);
    output.f_dielectric_fresnel_ibl = float3(0, 0, 0);
#endif // IMAGE_BASED_LIGHTING
    return output;
}

#endif // __LIGHTING_HLSLI__