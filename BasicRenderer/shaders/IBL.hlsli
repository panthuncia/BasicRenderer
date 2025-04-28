#ifndef __IBL_HLSLI__
#define __IBL_HLSLI__

#include "constants.hlsli"
#include "cbuffers.hlsli"
#include "PBRUtilites.hlsli"

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

float3 specularDFG(float2 DFG, float3 F0)
{
#if defined(SHADING_MODEL_CLOTH) // TODO: Other filament shading models
    return materialSurface.F0 * materialSurface.DFG.z;
#elif defined( SHADING_MODEL_SPECULAR_GLOSSINESS )
    return materialSurface.F0; // this seems to match better what the spec-gloss looks like in general
#else
    return lerp(DFG.xxx, DFG.yyy, F0);
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

float3 evaluateIBL(float3 normal, float3 bentNormal, float3 diffuseColor, float diffuseAO, float2 DFG, float3 F0, float3 reflection, in float roughness, in const uint environmentIndex, in const uint environmentBufferIndex)
{
    // Specular
    float3 E = specularDFG(DFG, F0); // Pre-calculated DFG term
    float3 r = getReflectedVector(reflection, normal, roughness);
    //Fr = E * prefilteredRadiance(materialSurface, r, materialSurface.PerceptualRoughness, useLocalIBL, useDistantIBL);

    float3 diffuseIrradiance = max(irradianceSH(normal, environmentIndex, environmentBufferIndex), 0.0) * Fd_Lambert();
    float3 Fd = diffuseColor * diffuseIrradiance * (1.0 - E) * diffuseAO;
    

    
    
    multiBounceAO(diffuseAO, diffuseColor, Fd);

    return diffuseColor * diffuseIrradiance;
}

#endif // __IBL_HLSLI__