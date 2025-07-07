#ifndef __GAMMA_CORRECTION_HLSLI__
#define __GAMMA_CORRECTION_HLSLI__

float3 LinearToSRGB(float3 color) {
    static const float invGamma = 1 / 2.2;
    return pow(color, float3(invGamma, invGamma, invGamma));
}

float3 SRGBToLinear(float3 color) {
    float gamma = 2.2;
    return pow(color, float3(gamma, gamma, gamma));
}

#endif // __GAMMA_CORRECTION_HLSLI__