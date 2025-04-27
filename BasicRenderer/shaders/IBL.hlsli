#ifndef __IBL_HLSLI__
#define __IBL_HLSLI__

#include "constants.hlsli"
#include "cbuffers.hlsli"
#include "PBRUtilites.hlsli"

float3 irradianceSH(float3 n, in const uint environmentIndex, in const uint environmentBufferIndex)
{
    StructuredBuffer<EnvironmentInfo> environments = ResourceDescriptorHeap[environmentBufferIndex];
    return
        environments[environmentIndex].sphericalHarmonics[0] * SH_FLOAT_SCALE_INVERSE
        + environments[environmentIndex].sphericalHarmonics[1] * SH_FLOAT_SCALE_INVERSE * (n.y)
        + environments[environmentIndex].sphericalHarmonics[2] * SH_FLOAT_SCALE_INVERSE * (n.z)
        + environments[environmentIndex].sphericalHarmonics[3] * SH_FLOAT_SCALE_INVERSE * (n.x)
        + environments[environmentIndex].sphericalHarmonics[4] * SH_FLOAT_SCALE_INVERSE * (n.y * n.x)
        + environments[environmentIndex].sphericalHarmonics[5] * SH_FLOAT_SCALE_INVERSE * (n.y * n.z)
        + environments[environmentIndex].sphericalHarmonics[6] * SH_FLOAT_SCALE_INVERSE * (3.0 * n.z * n.z - 1.0)
        + environments[environmentIndex].sphericalHarmonics[7] * SH_FLOAT_SCALE_INVERSE * (n.z * n.x)
        + environments[environmentIndex].sphericalHarmonics[8] * SH_FLOAT_SCALE_INVERSE * (n.x * n.x - n.y * n.y);
}

float3 evaluateIBL(float3 n, float3 diffuseColor, in const uint environmentIndex, in const uint environmentBufferIndex)
{
    float3 indirectDiffuse = max(irradianceSH(n, environmentIndex, environmentBufferIndex), 0.0) * Fd_Lambert();

    return diffuseColor * indirectDiffuse;
}

#endif // __IBL_HLSLI__