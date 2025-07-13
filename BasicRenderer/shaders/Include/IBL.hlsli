#ifndef __IBL_HLSLI__
#define __IBL_HLSLI__

#include "include/constants.hlsli"
#include "include/cbuffers.hlsli"
#include "include/PBR.hlsli"

float3 irradianceSH(float3 n, in const uint environmentIndex, in const uint environmentBufferIndex)
{
    StructuredBuffer<EnvironmentInfo> environments = ResourceDescriptorHeap[environmentBufferIndex];
    
    return
        float3(environments[environmentIndex].sphericalHarmonics[0], environments[environmentIndex].sphericalHarmonics[1], environments[environmentIndex].sphericalHarmonics[2]) * environments[environmentIndex].sphericalHarmonicsScale * SH_FLOAT_SCALE_INVERSE
       + float3(environments[environmentIndex].sphericalHarmonics[3], environments[environmentIndex].sphericalHarmonics[4], environments[environmentIndex].sphericalHarmonics[5]) * environments[environmentIndex].sphericalHarmonicsScale * SH_FLOAT_SCALE_INVERSE * n.y
       + float3(environments[environmentIndex].sphericalHarmonics[6], environments[environmentIndex].sphericalHarmonics[7], environments[environmentIndex].sphericalHarmonics[8]) * environments[environmentIndex].sphericalHarmonicsScale * SH_FLOAT_SCALE_INVERSE * n.z
       + float3(environments[environmentIndex].sphericalHarmonics[9], environments[environmentIndex].sphericalHarmonics[10], environments[environmentIndex].sphericalHarmonics[11]) * environments[environmentIndex].sphericalHarmonicsScale * SH_FLOAT_SCALE_INVERSE * n.x
       + float3(environments[environmentIndex].sphericalHarmonics[12], environments[environmentIndex].sphericalHarmonics[13], environments[environmentIndex].sphericalHarmonics[14]) * environments[environmentIndex].sphericalHarmonicsScale * SH_FLOAT_SCALE_INVERSE * n.y * n.x
       + float3(environments[environmentIndex].sphericalHarmonics[15], environments[environmentIndex].sphericalHarmonics[16], environments[environmentIndex].sphericalHarmonics[17]) * environments[environmentIndex].sphericalHarmonicsScale * SH_FLOAT_SCALE_INVERSE * n.y * n.z
       + float3(environments[environmentIndex].sphericalHarmonics[18], environments[environmentIndex].sphericalHarmonics[19], environments[environmentIndex].sphericalHarmonics[20]) * environments[environmentIndex].sphericalHarmonicsScale * SH_FLOAT_SCALE_INVERSE * (3.0 * n.z * n.z - 1.0)
       + float3(environments[environmentIndex].sphericalHarmonics[21], environments[environmentIndex].sphericalHarmonics[22], environments[environmentIndex].sphericalHarmonics[23]) * environments[environmentIndex].sphericalHarmonicsScale * SH_FLOAT_SCALE_INVERSE * n.z * n.x
       + float3(environments[environmentIndex].sphericalHarmonics[24], environments[environmentIndex].sphericalHarmonics[25], environments[environmentIndex].sphericalHarmonics[26]) * environments[environmentIndex].sphericalHarmonicsScale * SH_FLOAT_SCALE_INVERSE * (n.x * n.x - n.y * n.y);
        
}

/**
 * Returns a color ambient occlusion based on a pre-computed visibility term.
 * The albedo term is meant to be the diffuse color or f0 for the diffuse and
 * specular terms respectively.
 */
float3 gtaoMultiBounce(float visibility, const float3 albedo)
{
    // Jimenez et al. 2016, "Practical Realtime Strategies for Accurate Indirect Occlusion"
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c = 2.7552 * albedo + 0.6903;

    return max(float3(visibility.xxx), ((visibility * a + b) * visibility + c) * visibility);
}

void multiBounceAO(float visibility, const float3 albedo, inout float3 color)
{
//#if MULTI_BOUNCE_AMBIENT_OCCLUSION == 1 // Why would we not want this?
    color *= gtaoMultiBounce(visibility, albedo);
//#endif
}

void multiBounceSpecularAO(float visibility, const float3 albedo, inout float3 color)
{
//#if MULTI_BOUNCE_AMBIENT_OCCLUSION == 1 && SPECULAR_AMBIENT_OCCLUSION != SPECULAR_AO_OFF
    color *= gtaoMultiBounce(visibility, albedo);
//#endif
}

float SpecularAO_Lagarde(float NoV, float visibility, float roughness)
{
    // Lagarde and de Rousiers 2014, "Moving Frostbite to PBR"
    return saturate(pow(NoV + visibility, exp2(-16.0 * roughness - 1.0)) - 1.0 + visibility);
}

/*
Computes a specular occlusion term from the ambient occlusion term.
 */
float computeSpecularAO(float NdotV, float visibility, float roughness)
{
#if SPECULAR_AMBIENT_OCCLUSION == SPECULAR_AO_SIMPLE
    return SpecularAO_Lagarde(NdotV, visibility, roughness);
#elif SPECULAR_AMBIENT_OCCLUSION == SPECULAR_AO_BENT_NORMALS
    return SpecularAO_Cones(materialSurface, visibility, roughness);
#else
    return 1.0;
#endif
}

float3 getSpecularDominantDirection(const float3 n, const float3 r, float roughness)
{
    return lerp(r, n, roughness * roughness);
}

float3 getReflectedVector(float3 r, const float3 n, float roughness)
{
    // Anisotropy will change R
    return getSpecularDominantDirection(n, r, roughness);
}

float3 prefilteredRadiance(float3 reflection, float roughness, unsigned int prefilteredEnvironmentDescriptorIndex)
{
    
    TextureCube<float4> prefilteredEnvironment = ResourceDescriptorHeap[prefilteredEnvironmentDescriptorIndex];
    float lod = roughness * float(12 - 1);
    float4 specularSample = prefilteredEnvironment.SampleLevel(g_linearClamp, reflection, lod);
    return specularSample.rgb;
}

void combineDiffuseAndSpecular(const float3 n, const float3 E, const float3 Fd, const float3 Fr, inout float3 color)
{
#if defined(HAS_REFRACTION) // TODO: Refraction
    applyRefraction(materialSurface, n, E, Fd, Fr, color);
#else
    color += Fd + Fr;
#endif
}

void evaluateIBL(inout float3 color, inout float3 debugDiffuse, inout float3 debugSpecular, float3 normal, float3 bentNormal, float3 diffuseColor, float diffuseAO, float3 F0, float3 reflection, float roughness, float perceptualRoughness, float NdotV, in const uint environmentIndex, in const uint environmentBufferDescriptorIndex)
{
    
    // Specular
    float3 E = mx_ggx_dir_albedo_analytic(NdotV, roughness, F0, float3(1.0, 1.0, 1.0));
    float3 r = getReflectedVector(reflection, normal, roughness);
    
    StructuredBuffer<EnvironmentInfo> environments = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Environment::InfoBuffer)];
    float3 Fr = float3(0, 0, 0);
#if defined (PSO_SPECULAR_IBL)
    Fr = E * prefilteredRadiance(r, perceptualRoughness, environments[environmentIndex].prefilteredCubemapDescriptorIndex);
#endif
    float3 diffuseIrradiance = max(irradianceSH(normalize(normal + bentNormal), environmentIndex, environmentBufferDescriptorIndex), 0.0) * Fd_Lambert();
    float3 Fd = diffuseColor * diffuseIrradiance * (1.0 - E) * diffuseAO;
    
    // TODO: Subsurface and clearcoat
    
    multiBounceAO(diffuseAO, diffuseColor, Fd);
    
#if defined (PSO_SPECULAR_IBL)
    float specularAO = computeSpecularAO(NdotV, diffuseAO, roughness);
    multiBounceSpecularAO(specularAO, F0, Fr);
#endif
    combineDiffuseAndSpecular(normal, E, Fd, Fr, color);
    debugDiffuse = Fd;
    debugSpecular = Fr;
}

float3 evaluateSpecularIBL(float3 normal, float3 bentNormal, float diffuseAO, float3 F0, float3 reflection, float roughness, float perceptualRoughness, float NdotV, in const uint environmentIndex)
{
    float3 E = mx_ggx_dir_albedo_analytic(NdotV, roughness, F0, float3(1.0, 1.0, 1.0));
    float3 r = getReflectedVector(reflection, normal, roughness);
    StructuredBuffer<EnvironmentInfo> environments = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::EnvironmentInfo::InfoBuffer)];
    float3 Fr = E * prefilteredRadiance(r, perceptualRoughness, environments[environmentIndex].prefilteredCubemapDescriptorIndex);
    return Fr;
} 

float3 evaluateSpecularIBLFromSSR(float3 specularSample, float3 normal, float3 bentNormal, float diffuseAO, float3 F0, float roughness, float perceptualRoughness, float NdotV)
{
    float3 E = mx_ggx_dir_albedo_analytic(NdotV, roughness, F0, float3(1.0, 1.0, 1.0));
    //StructuredBuffer<EnvironmentInfo> environments = ResourceDescriptorHeap[environmentBufferDescriptorIndex];
    //float3 r = getReflectedVector(reflection, normal, roughness);
    float3 Fr = E * specularSample; //prefilteredRadiance(r, perceptualRoughness, environments[environmentIndex].prefilteredCubemapDescriptorIndex);
    float specularAO = computeSpecularAO(NdotV, diffuseAO, roughness);
    multiBounceSpecularAO(specularAO, F0, Fr);
    return Fr;
}

#endif // __IBL_HLSLI__