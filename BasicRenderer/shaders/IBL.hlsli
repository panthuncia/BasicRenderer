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

float3 evaluateIBL(float3 n, float3 diffuseColor, in const uint environmentIndex, in const uint environmentBufferIndex)
{
    float3 indirectDiffuse = max(irradianceSH(n, environmentIndex, environmentBufferIndex), 0.0) * Fd_Lambert();

    return diffuseColor * indirectDiffuse;
}

#endif // __IBL_HLSLI__