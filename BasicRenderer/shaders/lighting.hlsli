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

float3 calculateLightContribution(LightFragmentData light, LightingParameters lightingParameters) {
    
    // Unit vector halfway between view dir and light dir. Makes more accurate specular highlights.
    float3 halfwayDir = normalize(light.lightToFrag + lightingParameters.viewDir);
    
    float diff = max(dot(lightingParameters.normal, light.lightToFrag), 0.0);
    float3 diffuse = diff * light.lightColor;

    // Calculate specular light, Blinn-Phong
    float spec = pow(max(dot(lightingParameters.normal, halfwayDir), 0.0), 32.0);
    float3 specular = spec * light.lightColor;
    
    float3 lighting = (diffuse + specular) * light.attenuation;
    
    lighting *= light.spotAttenuation;
    
    return lighting * lightingParameters.albedo;
}

float3 calculateLightContributionPBR(LightFragmentData light, LightingParameters lightingParameters) {
    // Unit vector halfway between view dir and light dir. Makes more accurate specular highlights.
    float3 halfwayDir = normalize(light.lightToFrag + lightingParameters.viewDir);

    float3 radiance = light.lightColor * light.attenuation;
    float normDotLight = max(dot(lightingParameters.normal, light.lightToFrag), 0.0);

    // Cook-Torrence specular BRDF: fCookTorrance=DFG/(4(wo dot n)(wi dot n))
    // Approximate microfacet alignment
    float normalDistributionFunction = TrowbridgeReitzGGX(lightingParameters.normal, halfwayDir, lightingParameters.roughness);
    // Approximate microfacet shadowing
    float G = geometrySmith(lightingParameters.normal, lightingParameters.viewDir, lightingParameters.roughness, normDotLight);
    // Approximate specular intensity based on view angle
    float3 kSpecular = fresnelSchlickRoughness(max(dot(halfwayDir, lightingParameters.viewDir), 0.0), lightingParameters.F0, lightingParameters.roughness); // F

    // Preserve energy, diffuse+specular must be at most 1.0
    float3 kDiffuse = float3(1.0, 1.0, 1.0) - kSpecular;
    // Metallic surfaces have no diffuse color
    // model as diffuse color decreases as metallic fudge-factor increases 
    kDiffuse *= 1.0 - lightingParameters.metallic;

    float3 numerator = normalDistributionFunction * G * kSpecular;
    float denominator = 4.0 * max(dot(lightingParameters.normal, lightingParameters.viewDir), 0.0) * max(dot(lightingParameters.normal, light.lightToFrag), 0.0) + 0.0001; //+0.0001 fudge-factor to prevent division by 0
    float3 specular = numerator / denominator;

    float3 lighting = (kDiffuse * lightingParameters.albedo / PI + specular) * radiance * normDotLight;
    
    lighting *= light.spotAttenuation;
    
    return lighting * lightingParameters.albedo;
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
    uint materialFlags = materialInfo.materialFlags;
    float3 viewDir = normalize(mainCamera.positionWorldSpace.xyz - input.positionWorldSpace.xyz);
    
    float2 uv = float2(0.0, 0.0);
    if (materialFlags & MATERIAL_TEXTURED) {
        uv = input.texcoord;
        uv *= materialInfo.textureScale;
    }
    
    float3x3 TBN;
    if (materialInfo.materialFlags & MATERIAL_NORMAL_MAP || materialInfo.materialFlags & MATERIAL_PARALLAX) {
        //TBN = float3x3(input.TBN_T, input.TBN_B, input.TBN_N);
        TBN = cotangent_frame(input.normalWorldSpace.xyz, input.positionWorldSpace.xyz, uv);
    }

    float height = 0.0;
    
    if (materialInfo.materialFlags & MATERIAL_PARALLAX) {
        Texture2D<float> parallaxTexture = ResourceDescriptorHeap[materialInfo.heightMapIndex];
        SamplerState parallaxSamplerState = SamplerDescriptorHeap[materialInfo.heightSamplerIndex];
        float3 uvh = getContactRefinementParallaxCoordsAndHeight(parallaxTexture, parallaxSamplerState, TBN, uv, viewDir, materialInfo.heightMapScale);
        uv = uvh.xy;
    }
    
    
    float4 baseColor = float4(1.0, 1.0, 1.0, 1.0);
    
    if (materialFlags & MATERIAL_BASE_COLOR_TEXTURE) {
        Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[materialInfo.baseColorTextureIndex];
        SamplerState baseColorSamplerState = SamplerDescriptorHeap[materialInfo.baseColorSamplerIndex];
        baseColor = baseColorTexture.Sample(baseColorSamplerState, uv);
#if defined(PSO_ALPHA_TEST) || defined (PSO_BLEND)
        if (baseColor.a < materialInfo.alphaCutoff){
            discard;
        }
#endif // ALPHA_TEST
        baseColor.rgb = SRGBToLinear(baseColor.rgb);
    }

    if (materialFlags & MATERIAL_PBR) {
        baseColor = materialInfo.baseColorFactor * baseColor;
    }
    float3 normalWS = fragmentInfo.normalWS;
    
    if (materialFlags & MATERIAL_NORMAL_MAP) {
        Texture2D<float4> normalTexture = ResourceDescriptorHeap[materialInfo.normalTextureIndex];
        SamplerState normalSamplerState = SamplerDescriptorHeap[materialInfo.normalSamplerIndex];
        float3 textureNormal = normalTexture.Sample(normalSamplerState, uv).rgb;
        float3 tangentSpaceNormal = normalize(textureNormal * 2.0 - 1.0);
        if (materialFlags & MATERIAL_INVERT_NORMALS) {
            tangentSpaceNormal = -tangentSpaceNormal;
        }
        normalWS = normalize(mul(tangentSpaceNormal, TBN));
    }
    
#if defined(PSO_DOUBLE_SIDED)
    if(!isFrontFace) {
        normalWS = -normalWS;
    }
#endif // DOUBLE_SIDED
    
    float metallic = 0.0;
    float roughness = 0.0;
    float3 F0 = float3(0.04, 0.04, 0.04); // TODO: this should be specified per-material
    
    if (materialFlags & MATERIAL_PBR) {
        if (materialFlags & MATERIAL_PBR_MAPS) {
            Texture2D<float4> metallicTexture = ResourceDescriptorHeap[materialInfo.metallicTextureIndex];
            SamplerState metallicSamplerState = SamplerDescriptorHeap[materialInfo.metallicSamplerIndex];
            Texture2D<float4> roughnessTexture = ResourceDescriptorHeap[materialInfo.roughnessTextureIndex];
            SamplerState roughnessSamplerState = SamplerDescriptorHeap[materialInfo.roughnessSamplerIndex];
            metallic = metallicTexture.Sample(metallicSamplerState, uv).b * materialInfo.metallicFactor;
            roughness = roughnessTexture.Sample(roughnessSamplerState, uv).g * materialInfo.roughnessFactor;
        }
        else {
            metallic = materialInfo.metallicFactor;
            roughness = materialInfo.roughnessFactor;
        }
    }
    F0 = lerp(F0, baseColor.xyz, metallic);
    
    baseColor.xyz *= input.color;
    
    float3 lighting = float3(0.0, 0.0, 0.0);
    
    uint clusterIndex = 0; // Which light cluster this fragment belongs to
    uint clusterLightCount = 0; // Number of lights in the cluster
    
    if (enablePunctualLights) {
        LightingParameters lightingParameters;
        lightingParameters.fragPos = input.positionWorldSpace.xyz;
        lightingParameters.viewDir = viewDir;
        lightingParameters.normal = normalWS;
        lightingParameters.uv = uv;
        lightingParameters.albedo = baseColor.xyz;
        lightingParameters.metallic = metallic;
        lightingParameters.roughness = roughness;
        lightingParameters.F0 = F0;
        
        parallaxShadowParameters parallaxShadowParams;
        if (materialInfo.materialFlags & MATERIAL_PARALLAX) {
            parallaxShadowParams.parallaxTexture = ResourceDescriptorHeap[materialInfo.heightMapIndex];
            parallaxShadowParams.parallaxSampler = ResourceDescriptorHeap[materialInfo.heightSamplerIndex];
            parallaxShadowParams.TBN = TBN;
            parallaxShadowParams.heightmapScale = materialInfo.heightMapScale;
            parallaxShadowParams.lightToFrag = viewDir;
            parallaxShadowParams.viewDir = viewDir;
            parallaxShadowParams.uv = uv;
        }
        
        StructuredBuffer<unsigned int> pointShadowViewInfoIndexBuffer = ResourceDescriptorHeap[perFrameBuffer.pointLightCubemapBufferIndex];
        StructuredBuffer<unsigned int> spotShadowViewInfoIndexBuffer = ResourceDescriptorHeap[perFrameBuffer.spotLightMatrixBufferIndex];
        StructuredBuffer<unsigned int> directionalShadowViewInfoIndexBuffer = ResourceDescriptorHeap[perFrameBuffer.directionalLightCascadeBufferIndex];
        StructuredBuffer<Camera> cameraBuffer = ResourceDescriptorHeap[cameraBufferDescriptorIndex];
        
        StructuredBuffer<unsigned int> activeLightIndices = ResourceDescriptorHeap[perFrameBuffer.activeLightIndicesBufferIndex];
        StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[perFrameBuffer.lightBufferIndex];
        
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
        //roughness = activeCluster.near/10;
        // Loop through all pages of lights in the cluster
        uint pageIndex = activeCluster.ptrFirstPage;
        while (pageIndex != LIGHT_PAGE_ADDRESS_NULL) {
            for (uint i = 0; i < lightPagesBuffer[pageIndex].numLightsInPage; i++) {
                unsigned int index = activeLightIndices[lightPagesBuffer[pageIndex].lightIndices[i]];
#else
        for (uint i = 0; i < perFrameBuffer.numLights; i++) {
                unsigned int index = activeLightIndices[i];
#endif
                LightInfo light = lights[index];
                float shadow = 0.0;
                if (enableShadows) {
                    if (light.shadowViewInfoIndex != -1 && light.shadowMapIndex != -1) {
                        switch (light.type) {
                            case 0:{ // Point light
                                    shadow = calculatePointShadow(input.positionWorldSpace, normalWS.xyz, light, pointShadowViewInfoIndexBuffer, cameraBuffer);
                            //return float4(shadow, shadow, shadow, 1.0);
                                    break;
                                }
                            case 1:{ // Spot light
                                    uint spotShadowCameraIndex = spotShadowViewInfoIndexBuffer[light.shadowViewInfoIndex];
                                    Camera camera = cameraBuffer[spotShadowCameraIndex];
                                    shadow = calculateSpotShadow(input.positionWorldSpace, normalWS, light, camera.viewProjection, light.nearPlane, light.farPlane);
                                    break;
                                }
                            case 2:{// Directional light
                                    shadow = calculateCascadedShadow(input.positionWorldSpace, input.positionViewSpace, normalWS, light, perFrameBuffer.numShadowCascades, perFrameBuffer.shadowCascadeSplits, directionalShadowViewInfoIndexBuffer, cameraBuffer);
                                    //break;
                                }
                        }
                    }
                }
            
                LightFragmentData lightFragmentInfo = getLightParametersForFragment(light, input.positionWorldSpace.xyz);
                if (shadow > 0.95) {
                    continue; // skip light if shadowed
                }
                if (lightFragmentInfo.distance > light.maxRange && light.type != 2) {
                    continue;
                }
                if (materialInfo.materialFlags & MATERIAL_PBR && shadow) {
                    lighting += (1.0 - shadow) * calculateLightContributionPBR(lightFragmentInfo, lightingParameters);
                }
                else {
                    lighting += (1.0 - shadow) * calculateLightContribution(lightFragmentInfo, lightingParameters);
                }
                if (materialInfo.materialFlags & MATERIAL_PARALLAX) {
                    float parallaxShadow = getParallaxShadow(parallaxShadowParams);
                }
            }
#if defined(PSO_CLUSTERED_LIGHTING)
            pageIndex = lightPagesBuffer[pageIndex].ptrNextPage;
        }
#endif
    }
    float ao = 1.0;
    if (materialInfo.materialFlags & MATERIAL_AO_TEXTURE) {
        Texture2D<float4> aoTexture = ResourceDescriptorHeap[materialInfo.aoMapIndex];
        SamplerState aoSamplerState = SamplerDescriptorHeap[materialInfo.aoSamplerIndex];
        ao = aoTexture.Sample(aoSamplerState, uv).r;
    }
    
#if defined(PSO_IMAGE_BASED_LIGHTING)
  
    // irradiance
    TextureCube<float4> irradianceMap = ResourceDescriptorHeap[perFrameBuffer.environmentIrradianceMapIndex];
    SamplerState irradianceSampler = SamplerDescriptorHeap[perFrameBuffer.environmentIrradianceSamplerIndex];
    float3 f_diffuse = getDiffuseLight(normalWS, irradianceMap, irradianceSampler) * baseColor.rgb;
    
    // metallic and dielectric specular
    TextureCube<float4> prefilteredEnvironment = ResourceDescriptorHeap[perFrameBuffer.environmentPrefilteredMapIndex];
    SamplerState prefilteredSampler = SamplerDescriptorHeap[perFrameBuffer.environmentPrefilteredSamplerIndex];
    float3 f_specular_metal = getIBLRadianceGGX(normalWS, viewDir, roughness, prefilteredEnvironment, prefilteredSampler);
    float3 f_specular_dielectric = f_specular_metal;
    
    // Metallic fresnel
    Texture2D<float2> brdfLUT = ResourceDescriptorHeap[perFrameBuffer.environmentBRDFLUTIndex];
    SamplerState brdfSampler = SamplerDescriptorHeap[perFrameBuffer.environmentBRDFLUTSamplerIndex];
    float3 f_metal_fresnel_ibl = getIBLGGXFresnel(normalWS, viewDir, roughness, F0, 1.0, brdfLUT, brdfSampler);
    float3 f_metal_brdf_ibl = f_metal_fresnel_ibl * f_specular_metal;
    
    // Dielectric fresnel
    float specularWeight = 1.0;
    float3 f_dielectric_fresnel_ibl = getIBLGGXFresnel(normalWS, viewDir, roughness, F0, specularWeight, brdfLUT, brdfSampler);
    float3 f_dielectric_brdf_ibl = lerp(f_diffuse, f_specular_dielectric, f_dielectric_fresnel_ibl);
    
    float3 ambient = lerp(f_dielectric_brdf_ibl, f_metal_brdf_ibl, metallic);
    
#else 
    float3 ambient = perFrameBuffer.ambientLighting.xyz * baseColor.xyz;
#endif // IMAGE_BASED_LIGHTING
    if (enableGTAO) {
        ao *= fragmentInfo.ambientOcclusion;
    }
    ambient *= ao;
    lighting += ambient;
    if (meshBuffer.vertexFlags & VERTEX_COLORS) { // TODO: This only makes sense for forward rendering
        lighting = lighting * input.color.xyz;
    }
    
    float3 emissive = float3(0.0, 0.0, 0.0);
    if (materialInfo.materialFlags & MATERIAL_EMISSIVE_TEXTURE) {
        Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[materialInfo.emissiveTextureIndex];
        SamplerState emissiveSamplerState = SamplerDescriptorHeap[materialInfo.emissiveSamplerIndex];
        emissive = SRGBToLinear(emissiveTexture.Sample(emissiveSamplerState, uv).rgb) * materialInfo.emissiveFactor.rgb;
        lighting += emissive;
    }
    else {
        lighting += materialInfo.emissiveFactor.rgb;
    }
    
    LightingOutput output;
    output.lighting = lighting;
    output.baseColor = baseColor;
    output.normalWS = normalWS;
    output.metallic = metallic;
    output.roughness = roughness;
    output.ao = ao;
    output.emissive = emissive;
    output.viewDir = viewDir;
    output.clusterID = clusterIndex;
    output.clusterLightCount = clusterLightCount;
#if defined(PSO_IMAGE_BASED_LIGHTING)
    output.f_metal_brdf_ibl = f_metal_brdf_ibl;
    output.f_dielectric_brdf_ibl = f_dielectric_brdf_ibl;
    output.f_specular_metal = f_specular_metal;
    output.f_metal_fresnel_ibl = f_metal_fresnel_ibl;
    output.f_dielectric_fresnel_ibl = f_dielectric_fresnel_ibl;
#endif // IMAGE_BASED_LIGHTING
    
    return output;
}

#endif // __LIGHTING_HLSLI__