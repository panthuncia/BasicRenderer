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
    float intensity;
    float3 lightToFrag;
    float attenuation;
    float distance;
    float spotAttenuation;
};

struct LightingParameters {
    float3 fragPos;
    float3 viewDir;
    float3 normal;
    float3 diffuseColor;
    float metallic;
    float roughness;
    float3 F0;
};

struct LightingOutput { // Lighting + debug info
    float3 lighting;
#if defined(PSO_IMAGE_BASED_LIGHTING)
    float3 diffuseIBL;
    float3 specularIBL;
#endif // IMAGE_BASED_LIGHTING
#if defined(PSO_CLUSTERED_LIGHTING)
    uint clusterIndex;
    uint clusterLightCount;
#endif
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
    result.intensity = light.color.w;
    
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


float3 calculateLightContributionPBR(LightFragmentData light, LightingParameters lightingParameters)
{

    float3 halfwayDir = normalize(light.lightToFrag + lightingParameters.viewDir);
    float normDotView = saturate(dot(lightingParameters.normal, lightingParameters.viewDir));
    float normDotLight = saturate(dot(lightingParameters.normal, light.lightToFrag));
    float normDotHalf = saturate(dot(lightingParameters.normal, halfwayDir));
    float lightDotHalf = saturate(dot(light.lightToFrag, halfwayDir));
    
    float3 Fr = specularLobe(lightingParameters.roughness, lightingParameters.F0, halfwayDir, normDotView, normDotLight, normDotHalf, lightDotHalf);
    
    float3 Fd = diffuseLobe(lightingParameters.roughness, lightingParameters.diffuseColor, normDotView, normDotLight, lightDotHalf);

    float3 energyCompensation = mx_ggx_energy_compensation(normDotView, lightingParameters.roughness, lightingParameters.F0);
    
    float3 BRDF = Fd + Fr * energyCompensation;
    
    return BRDF * light.lightColor.rgb * light.intensity * light.attenuation * light.spotAttenuation * normDotLight;
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
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    float3 lighting = float3(0.0, 0.0, 0.0);
    float3 debugDiffuse = float3(0, 0, 0);
    float3 debugSpecular = float3(0, 0, 0);
    
#if defined(PSO_IMAGE_BASED_LIGHTING)
    evaluateIBL(lighting,
                debugDiffuse,
                debugSpecular,
                fragmentInfo.normalWS, 
                fragmentInfo.normalWS, 
                fragmentInfo.diffuseColor, 
                fragmentInfo.diffuseAmbientOcclusion, 
                fragmentInfo.F0, 
                fragmentInfo.reflectedWS, 
                fragmentInfo.roughness,
                fragmentInfo.perceptualRoughness,
                fragmentInfo.NdotV,
                activeEnvironmentIndex, 
                environmentBufferDescriptorIndex);
#endif // IMAGE_BASED_LIGHTING
    
    // Direct lighting
    
    if (enablePunctualLights)
    {
        LightingParameters lightingParameters;
        lightingParameters.fragPos = input.positionWorldSpace.xyz;
        lightingParameters.viewDir = fragmentInfo.viewWS;
        lightingParameters.normal = fragmentInfo.normalWS;
        lightingParameters.diffuseColor = fragmentInfo.diffuseColor;
        lightingParameters.metallic = fragmentInfo.metallic;
        lightingParameters.roughness = fragmentInfo.roughness;
        lightingParameters.F0 = fragmentInfo.F0;
        
        /* // TODO: Parallax shadows will require a forward pass
        parallaxShadowParameters parallaxShadowParams;
        if (materialInfo.materialFlags & MATERIAL_PARALLAX)
        {
            parallaxShadowParams.parallaxTexture = ResourceDescriptorHeap[materialInfo.heightMapIndex];
            parallaxShadowParams.parallaxSampler = ResourceDescriptorHeap[materialInfo.heightSamplerIndex];
            parallaxShadowParams.TBN = TBN;
            parallaxShadowParams.heightmapScale = materialInfo.heightMapScale;
            parallaxShadowParams.lightToFrag = viewDir;
            parallaxShadowParams.viewDir = viewDir;
            parallaxShadowParams.uv = uv;
        }*/
        
        StructuredBuffer<unsigned int> pointShadowViewInfoIndexBuffer = ResourceDescriptorHeap[perFrameBuffer.pointLightCubemapBufferIndex];
        StructuredBuffer<unsigned int> spotShadowViewInfoIndexBuffer = ResourceDescriptorHeap[perFrameBuffer.spotLightMatrixBufferIndex];
        StructuredBuffer<unsigned int> directionalShadowViewInfoIndexBuffer = ResourceDescriptorHeap[perFrameBuffer.directionalLightCascadeBufferIndex];
        StructuredBuffer<Camera> cameraBuffer = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
        
        StructuredBuffer<unsigned int> activeLightIndices = ResourceDescriptorHeap[perFrameBuffer.activeLightIndicesBufferIndex];
        StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[perFrameBuffer.lightBufferIndex];
        
        uint clusterIndex = 0; // Which light cluster this fragment belongs to
        uint clusterLightCount = 0; // Number of lights in the cluster
#if defined(PSO_CLUSTERED_LIGHTING)
        
        StructuredBuffer<Cluster> clusterBuffer = ResourceDescriptorHeap[lightClusterBufferDescriptorIndex];
        StructuredBuffer<LightPage> lightPagesBuffer = ResourceDescriptorHeap[lightPagesBufferDescriptorIndex];
        
        float3 clusterID = ComputeClusterID(input.position, input.positionViewSpace.z, perFrameBuffer, cameraBuffer[perFrameBuffer.mainCameraIndex]);
        clusterIndex = clusterID.x +
                        clusterID.y * perFrameBuffer.lightClusterGridSizeX +
                        clusterID.z * perFrameBuffer.lightClusterGridSizeX * perFrameBuffer.lightClusterGridSizeY;
        
        Cluster activeCluster = clusterBuffer[clusterIndex];
        
        clusterIndex = clusterID.z;

        clusterLightCount = activeCluster.numLights;
        // Loop through all pages of lights in the cluster
        uint pageIndex = activeCluster.ptrFirstPage;
        while (pageIndex != LIGHT_PAGE_ADDRESS_NULL) {
            for (uint i = 0; i < lightPagesBuffer[pageIndex].numLightsInPage; i++) {
                unsigned int index = activeLightIndices[lightPagesBuffer[pageIndex].lightIndices[i]];
#else
        for (uint i = 0; i < perFrameBuffer.numLights; i++)
        {
            unsigned int index = activeLightIndices[i];
#endif
            LightInfo light = lights[index];
            float shadow = 0.0;
            if (enableShadows)
            {
                if (light.shadowViewInfoIndex != -1 && light.shadowMapIndex != -1)
                {
                    switch (light.type)
                    {
                    case 0:{ // Point light
                            shadow = calculatePointShadow(input.positionWorldSpace, fragmentInfo.normalWS.xyz, light, pointShadowViewInfoIndexBuffer, cameraBuffer);
                        //return float4(shadow, shadow, shadow, 1.0);
                            break;
                        }
                    case 1:{ // Spot light
                            uint spotShadowCameraIndex = spotShadowViewInfoIndexBuffer[light.shadowViewInfoIndex];
                            Camera camera = cameraBuffer[spotShadowCameraIndex];
                            shadow = calculateSpotShadow(input.positionWorldSpace, fragmentInfo.normalWS, light, camera.viewProjection, light.nearPlane, light.farPlane);
                            break;
                        }
                    case 2:{// Directional light
                            shadow = calculateCascadedShadow(input.positionWorldSpace, input.positionViewSpace, fragmentInfo.normalWS, light, perFrameBuffer.numShadowCascades, perFrameBuffer.shadowCascadeSplits, directionalShadowViewInfoIndexBuffer, cameraBuffer);
                                //break;
                        }
                    }
                }
            }
            
            LightFragmentData lightFragmentInfo = getLightParametersForFragment(light, input.positionWorldSpace.xyz);
            if (shadow > 0.95)
            {
                continue; // skip light if shadowed
            }
            if (lightFragmentInfo.distance > light.maxRange && light.type != 2)
            {
                continue;
            }
            lighting += (1.0 - shadow) * calculateLightContributionPBR(lightFragmentInfo, lightingParameters);
            /*if (materialInfo.materialFlags & MATERIAL_PARALLAX)
            {
                float parallaxShadow = getParallaxShadow(parallaxShadowParams);
            }*/
        }
#if defined(PSO_CLUSTERED_LIGHTING)
            pageIndex = lightPagesBuffer[pageIndex].ptrNextPage;
        }
#endif
    }
    
    LightingOutput output;
    output.lighting = lighting;
    
#if defined(PSO_IMAGE_BASED_LIGHTING)
    output.diffuseIBL = debugDiffuse;
    output.specularIBL = debugSpecular;
#endif // IMAGE_BASED_LIGHTING
    return output;
}

#endif // __LIGHTING_HLSLI__