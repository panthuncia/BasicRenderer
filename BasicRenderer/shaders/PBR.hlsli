#ifndef __PBR_HLSLI__
#define __PBR_HLSLI__

#include "constants.hlsli"

// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html
// http://ix.cs.uoregon.edu/~hank/441/lectures/pbr_slides.pdf
// https://learnopengl.com/PBR/Theory
// Most of this is "plug-and-chug", because I'm not deriving my own BRDF or Fresnel equations

// Approximates the percent of microfacets in a surface aligned with the halfway vector
float TrowbridgeReitzGGX(float3 normalDir, float3 halfwayDir, float roughness) {
    // UE4 uses alpha = roughness^2, so I will too.
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
        
    float normDotHalf = max(dot(normalDir, halfwayDir), 0.0);
    float normDotHalf2 = normDotHalf * normDotHalf;

    float denom1 = (normDotHalf2 * (alpha2 - 1.0) + 1.0);
    float denom2 = denom1 * denom1;

    return alpha2 / (PI * denom2);
}
// Approximates self-shadowing of microfacets on a surface
float geometrySchlickGGX(float normDotView, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float denominator = normDotView * (1.0 - k) + k;
    return normDotView / denominator;
}
float geometrySmith(float3 normalDir, float3 viewDir, float roughness, float normDotLight) {
    float normDotView = max(dot(normalDir, viewDir), 0.0);

    // combination of shadowing from microfacets obstructing view vector, and microfacets obstructing light vector
    return geometrySchlickGGX(normDotView, roughness) * geometrySchlickGGX(normDotLight, roughness);
}

// models increased reflectivity as view angle approaches 90 degrees
float3 fresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Variant of Schlick's approximation that accounts for roughness, used for image-based lighting
float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float invRoughness = 1.0 - roughness;
    return F0 + (max(float3(invRoughness, invRoughness, invRoughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float clampedDot(float3 x, float3 y) {
    return clamp(dot(x, y), 0.0, 1.0);
}

float4 getSpecularSample(float3 reflection, float lod, TextureCube<float4> prefilteredEnvironment, SamplerState environmentSampler) {
    float4 textureSample = prefilteredEnvironment.SampleLevel(environmentSampler, reflection, lod); //textureLod(u_GGXEnvSampler, u_EnvRotation * reflection, lod);
    textureSample.rgb *= 2.0; // u_EnvIntensity;
    return textureSample;
}

float3 getDiffuseLight(float3 n, TextureCube<float4> environmentRadiance, SamplerState radianceSampler) {
    float3 textureSample = environmentRadiance.Sample(radianceSampler, n).rgb; //texture(u_LambertianEnvSampler, u_EnvRotation * n);
    textureSample.rgb *= 1.0; //0.3; //u_EnvIntensity;
    return textureSample.rgb;
}

float3 getIBLRadianceGGX(float3 n, float3 v, float roughness, TextureCube<float4> prefilteredEnvironment, SamplerState environmentSampler) {
    float NdotV = clampedDot(n, v);
    float lod = roughness * float(12 - 1);
    float3 reflection = normalize(reflect(-v, n));
    float4 specularSample = getSpecularSample(reflection, lod, prefilteredEnvironment, environmentSampler);

    float3 specularLight = specularSample.rgb;

    return specularLight;
}

float3 getIBLGGXFresnel(float3 n, float3 v, float roughness, float3 F0, float specularWeight, Texture2D<float2> LUT, SamplerState samplerState) {
    // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
    // Roughness dependent fresnel, from Fdez-Aguera
    float NdotV = clampedDot(n, v);
    float2 brdfSamplePoint = clamp(float2(NdotV, roughness), float2(0.0, 0.0), float2(1.0, 1.0));
    float2 f_ab = LUT.Sample(samplerState, brdfSamplePoint); // texture(u_GGXLUT, brdfSamplePoint).rg;
    float invRoughness = 1.0 - roughness;
    float3 Fr = max(float3(invRoughness, invRoughness, invRoughness), F0) - F0;
    float3 k_S = F0 + Fr * pow(1.0 - NdotV, 5.0);
    float3 FssEss = specularWeight * (k_S * f_ab.x + f_ab.y);

    // Multiple scattering, from Fdez-Aguera
    float Ems = (1.0 - (f_ab.x + f_ab.y));
    float3 F_avg = specularWeight * (F0 + (1.0 - F0) / 21.0);
    float3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);

    return FssEss + FmsEms;
}

#endif // __PBR_HLSLI__