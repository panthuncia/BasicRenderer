#define PI 3.1415926538

struct PerFrameBuffer {
    row_major matrix view;
    row_major matrix projection;
    float4 eyePosWorldSpace;
    float4 ambientLighting;
    float4 shadowCascadeSplits;
    uint lightBufferIndex;
    uint numLights;
    uint pointLightCubemapBufferIndex;
    uint spotLightMatrixBufferIndex;
    uint directionalLightCascadeBufferIndex;
    uint numShadowCascades;
    uint environmentIrradianceMapIndex;
    uint environmentIrradianceSamplerIndex;
    uint environmentPrefilteredMapIndex;
    uint environmentPrefilteredSamplerIndex;
    uint environmentBRDFLUTIndex;
    uint environmentBRDFLUTSamplerIndex;
};

cbuffer PerObject : register(b1) {
    row_major matrix model;
    row_major float4x4 normalMatrix;
    uint boneTransformBufferIndex;
    uint inverseBindMatricesBufferIndex;
};

cbuffer PerMesh : register(b2) {
    uint materialDataIndex;
};

cbuffer RootConstants1 : register(b3) {
    int currentLightID; // Used for shadow mapping, global light index
};

cbuffer RootConstants2 : register(b4) {
    int lightViewIndex; // Used for shadow mapping, index in light type's shadow view matrix array
}

cbuffer RootConstants3 : register(b5) {
    matrix shadowViewMatrix; // Used for shadow mapping, shadow view matrix
}

struct LightInfo {
    uint type;
    float innerConeAngle;
    float outerConeAngle;
    int shadowViewInfoIndex; // -1 if no shadow map
    float4 posWorldSpace; // Position of the light
    float4 dirWorldSpace; // Direction of the light
    float4 attenuation; // x,y,z = constant, linear, quadratic attenuation, w= max range
    float4 color; // Color of the light
    float nearPlane;
    float farPlane;
    int shadowMapIndex;
    int shadowSamplerIndex;
};

struct MaterialInfo {
    uint psoFlags;
    uint baseColorTextureIndex;
    uint baseColorSamplerIndex;
    uint normalTextureIndex;
    uint normalSamplerIndex;
    uint metallicRoughnessTextureIndex;
    uint metallicRoughnessSamplerIndex;
    uint emissiveTextureIndex;
    uint emissiveSamplerIndex;
    uint aoMapIndex;
    uint aoSamplerIndex;
    uint heightMapIndex;
    uint heightSamplerIndex;
    float metallicFactor;
    float roughnessFactor;
    float ambientStrength;
    float specularStrength;
    float textureScale;
    float heightMapScale;
    float pad0;
    float4 baseColorFactor;
    float4 emissiveFactor;
};

struct SingleMatrix {
    row_major matrix value;
};


#if defined(VERTEX_COLORS)
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
};
#else
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
#if defined(TEXTURED)
    float2 texcoord : TEXCOORD0;
#endif
#if defined(NORMAL_MAP) || defined(PARALLAX)
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
#endif // NORMAL_MAP
#if defined(SKINNED)
    uint4 joints : TEXCOORD1;
    float4 weights : TEXCOORD2;
#endif // SKINNED
};
#endif

#if defined(VERTEX_COLORS)
struct PSInput {
    float4 position : SV_POSITION;
    float4 positionWorldSpace : TEXCOORD3;
    float4 positionViewSpace : TEXCOORD4;
    float3 normalWorldSpace : TEXCOORD5;
    float4 color : COLOR;
};
#else
struct PSInput {
    float4 position : SV_POSITION;
    float4 positionWorldSpace : TEXCOORD3;
    float4 positionViewSpace : TEXCOORD4;
    float3 normalWorldSpace : TEXCOORD5;
#if defined(TEXTURED)
    float2 texcoord : TEXCOORD0;
#endif // TEXTURED
#if defined(NORMAL_MAP) || defined(PARALLAX)
    float3 TBN_T : TEXCOORD6;      // First row of TBN
    float3 TBN_B : TEXCOORD7;      // Second row of TBN
    float3 TBN_N : TEXCOORD8;      // Third row of TBN
#endif // NORMAL_MAP
};
#endif

matrix loadMatrixFromBuffer(StructuredBuffer<float4> matrixBuffer, uint matrixIndex) {
    float4 bone1Row1 = matrixBuffer[matrixIndex * 4];
    float4 bone1Row2 = matrixBuffer[matrixIndex * 4 + 1];
    float4 bone1Row3 = matrixBuffer[matrixIndex * 4 + 2];
    float4 bone1Row4 = matrixBuffer[matrixIndex * 4 + 3];
    return float4x4(bone1Row1, bone1Row2, bone1Row3, bone1Row4);
}

PSInput VSMain(VSInput input) {
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    float4 pos = float4(input.position.xyz, 1.0f);
    
    float3x3 normalMatrixSkinnedIfNecessary = (float3x3)normalMatrix;
    
#if defined(SKINNED)
    StructuredBuffer<float4> boneTransformsBuffer = ResourceDescriptorHeap[boneTransformBufferIndex];
    StructuredBuffer<float4> inverseBindMatricesBuffer = ResourceDescriptorHeap[inverseBindMatricesBufferIndex];
    
    matrix bone1 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.x);
    matrix bone2 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.y);
    matrix bone3 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.z);
    matrix bone4 = loadMatrixFromBuffer(boneTransformsBuffer, input.joints.w);
    
    matrix bindMatrix1 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.x);
    matrix bindMatrix2 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.y);
    matrix bindMatrix3 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.z);
    matrix bindMatrix4 = loadMatrixFromBuffer(inverseBindMatricesBuffer, input.joints.w);

    matrix skinMatrix = input.weights.x * mul(bindMatrix1, bone1) +
                        input.weights.y * mul(bindMatrix2, bone2) +
                        input.weights.z * mul(bindMatrix3, bone3) +
                        input.weights.w * mul(bindMatrix4, bone4);
    
    pos = mul(pos, skinMatrix);
    normalMatrixSkinnedIfNecessary = mul(normalMatrixSkinnedIfNecessary, (float3x3)skinMatrix);
#endif // SKINNED
    
    PSInput output;
    float4 worldPosition = mul(pos, model);
    
#if defined(SHADOW)
    StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[perFrameBuffer.lightBufferIndex];
    LightInfo light = lights[currentLightID];
    matrix lightMatrix;
    switch(light.type) {
        case 0: { // Point light
            StructuredBuffer<float4> pointLightCubemapBuffer = ResourceDescriptorHeap[perFrameBuffer.pointLightCubemapBufferIndex];
            lightMatrix = loadMatrixFromBuffer(pointLightCubemapBuffer, lightViewIndex);
            break;
        }
        case 1: { // Spot light
            StructuredBuffer<float4> spotLightMatrixBuffer = ResourceDescriptorHeap[perFrameBuffer.spotLightMatrixBufferIndex];
            lightMatrix = loadMatrixFromBuffer(spotLightMatrixBuffer, lightViewIndex);
            break;
        }
        case 2: { // Directional light
            StructuredBuffer<float4> directionalLightCascadeBuffer = ResourceDescriptorHeap[perFrameBuffer.directionalLightCascadeBufferIndex];
            lightMatrix = loadMatrixFromBuffer(directionalLightCascadeBuffer, lightViewIndex);
            break;
        }
    }
    output.position = mul(worldPosition, lightMatrix);
    return output;
#endif // SHADOW
    output.positionWorldSpace = worldPosition;
    float4 viewPosition = mul(worldPosition, perFrameBuffer.view);
    output.positionViewSpace = viewPosition;
    output.position = mul(viewPosition, perFrameBuffer.projection);
    
    output.normalWorldSpace = normalize(mul(input.normal, normalMatrixSkinnedIfNecessary));
    
#if defined(NORMAL_MAP) || defined(PARALLAX)
    output.TBN_T = normalize(mul(input.tangent, normalMatrixSkinnedIfNecessary));
    output.TBN_B = normalize(mul(input.bitangent, normalMatrixSkinnedIfNecessary));
    output.TBN_N = normalize(mul(input.normal, normalMatrixSkinnedIfNecessary));
#endif // NORMAL_MAP
    
#if defined(VERTEX_COLORS)
    output.color = input.color;
#endif
#if defined(TEXTURED)
    output.texcoord = input.texcoord;
#endif
    return output;
}

float3 LinearToSRGB(float3 color) {
    static const float invGamma = 1/2.2;
    return pow(color, float3(invGamma, invGamma, invGamma));
}

float3 SRGBToLinear(float3 color) {
    float gamma = 2.2;
    return pow(color, float3(gamma, gamma, gamma));
}

// Parallax shadowing, very expensive method (per-fragment*per-light tangent-space raycast)
float getParallaxShadow(Texture2D<float> parallaxTexture, SamplerState parallaxSampler, float3x3 TBN, float2 uv, float3 lightDir, float3 viewDir, float sampleHeight, float heightmapScale) {
    lightDir = normalize(mul(TBN, lightDir));
    int steps = 8;
    float maxDistance = heightmapScale * 0.2; //0.1;
    float currentHeight = parallaxTexture.Sample(parallaxSampler, uv); //texture(u_heightMap, uv).r;
    float2 lightDirUV = normalize(lightDir.xy);
    float heightStep = lightDir.z / float(steps);
    float stepSizeUV = maxDistance / float(steps);

    for (int i = 0; i < steps; ++i) {
        uv += lightDirUV * stepSizeUV; // Step across
        currentHeight += heightStep; // Step up
            
        float heightAtSample = parallaxTexture.Sample(parallaxSampler, uv); //texture(u_heightMap, uv).r;
    
        if (heightAtSample > currentHeight) {
            return 0.05;
        }
    }
    
    return 1.0;
}

float2 WrapFloat2(float2 input) {
    // Apply modulo 1.0 and handle negative values by adding 1.0 and taking modulo again
    return frac(input + 1.0);
}

// Contact-refinement parallax 
// https://www.artstation.com/blogs/andreariccardi/3VPo/a-new-approach-for-parallax-mapping-presenting-the-contact-refinement-parallax-mapping-technique
float3 getContactRefinementParallaxCoordsAndHeight(Texture2D<float> parallaxTexture, SamplerState parallaxSampler, float3x3 TBN, float2 uv, float3 viewDir, float heightmapScale) {
    // Get view direction in tangent space
    uv.y = 1.0- uv.y;
    viewDir = normalize(mul(TBN, viewDir));

    float maxHeight = heightmapScale; //0.05;
    float minHeight = maxHeight * 0.5;

    int numSteps = 16;
    // Corrects for Z view angle
    float viewCorrection = (-viewDir.z) + 2.0;
    float stepSize = 1.0 / (float(numSteps) + 1.0);
    float2 stepOffset = viewDir.xy * float2(maxHeight, maxHeight) * stepSize;

    float2 lastOffset = WrapFloat2(viewDir.xy * float2(minHeight, minHeight) + uv);
    float lastRayDepth = 1.0;
    float lastHeight = 1.0;

    float2 p1;
    float2 p2;
    bool refine = false;

    while (numSteps > 0) {
        // Advance ray in direction of TS view direction
        float2 candidateOffset = WrapFloat2(lastOffset - stepOffset);

        float currentRayDepth = lastRayDepth - stepSize;

        // Sample height map at this offset
        float currentHeight = parallaxTexture.Sample(parallaxSampler, candidateOffset); //texture(u_heightMap, candidateOffset).r;
        currentHeight = viewCorrection * currentHeight;
        // Test our candidate depth
        if (currentHeight > currentRayDepth) {
            p1 = float2(currentRayDepth, currentHeight);
            p2 = float2(lastRayDepth, lastHeight);
            // Break if this is the contact refinement pass
            if (refine) {
                lastHeight = currentHeight;
                break;
            // Else, continue raycasting with squared precision
            }
            else {
                refine = true;
                lastRayDepth = p2.x;
                stepSize /= float(numSteps);
                stepOffset /= float(numSteps);
                continue;
            }
        }
        lastOffset = candidateOffset;
        lastRayDepth = currentRayDepth;
        lastHeight = currentHeight;
        numSteps -= 1;
    }
    // Interpolate between final two points
    float diff1 = p1.x - p1.y;
    float diff2 = p2.x - p2.y;
    float denominator = diff2 - diff1;

    float parallaxAmount;
    if (denominator != 0.0) {
        parallaxAmount = (p1.x * diff2 - p2.x * diff1) / denominator;
    }

    float offset = ((1.0 - parallaxAmount) * -maxHeight) + minHeight;
    return float3(viewDir.xy * offset + uv, lastHeight);
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

#if defined(PARALLAX)
float3 calculateLightContribution(LightInfo light, float3 fragPos, float3 viewDir, float3 normal, float2 uv, float3 albedo, float metallic, float roughness, float3 F0, float height, Texture2D<float> parallaxTexture, SamplerState parallaxSampler, float3x3 TBN, float heightmapScale) {
#else
float3 calculateLightContribution(LightInfo light, float3 fragPos, float3 viewDir, float3 normal, float2 uv, float3 albedo, float metallic, float roughness, float3 F0) {
#endif
    uint lightType = light.type;
    float3 lightPos = light.posWorldSpace.xyz;
    float3 lightColor = light.color.xyz;
    float3 dir = light.dirWorldSpace.xyz;
    float constantAttenuation = light.attenuation.x;
    float linearAttenuation = light.attenuation.y;
    float quadraticAttenuation = light.attenuation.z;
    
    float outerConeCos = light.outerConeAngle;
    float innerConeCos = light.innerConeAngle;
    
    float3 lightDir;
    float distance;
    float attenuation;
    float spotAttenuationFactor = 0;
    
    // For directional lights, use light dir directly, with zero attenuation
    switch (lightType) {
        case 2:{
            lightDir = -dir;
            attenuation = 1.0;
            break;
        }
        default:{
            lightDir = normalize(lightPos - fragPos);
            distance = length(lightPos - fragPos);
            attenuation = 1.0 / ((constantAttenuation + linearAttenuation * distance + quadraticAttenuation * distance * distance) + 0.0001); //+0.0001 fudge-factor to prevent division by 0;
            break;
        }
    }
    
    // Unit vector halfway between view dir and light dir. Makes more accurate specular highlights.
    float3 halfwayDir = normalize(lightDir + viewDir);

#if defined(PBR)
    float3 radiance = lightColor * attenuation;
    float normDotLight = max(dot(normal, lightDir), 0.0);

    // Cook-Torrence specular BRDF: fCookTorrance=DFG/(4(wo dot n)(wi dot n))
    // Approximate microfacet alignment
    float normalDistributionFunction = TrowbridgeReitzGGX(normal, halfwayDir, roughness);
    // Approximate microfacet shadowing
    float G = geometrySmith(normal, viewDir, roughness, normDotLight);
    // Approximate specular intensity based on view angle
    float3 kSpecular = fresnelSchlick(max(dot(halfwayDir, viewDir), 0.0), F0); // F

    // Preserve energy, diffuse+specular must be at most 1.0
    float3 kDiffuse = float3(1.0, 1.0, 1.0) - kSpecular;
    // Metallic surfaces have no diffuse color
    // model as diffuse color decreases as metallic fudge-factor increases 
    kDiffuse *= 1.0 - metallic;

    float3 numerator = normalDistributionFunction * G * kSpecular;
    float denominator = 4.0 * max(dot(normal, viewDir), 0.0) * max(dot(normal, lightDir), 0.0) + 0.0001; //+0.0001 fudge-factor to prevent division by 0
    float3 specular = numerator / denominator;

    float3 lighting = (kDiffuse * albedo / PI + specular) * radiance * normDotLight;
#else
    
    float diff = max(dot(normal, lightDir), 0.0);
    float3 diffuse = diff * lightColor;

    // Calculate specular light, Blinn-Phong
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
    float3 specular = /*u_specularStrength * */ spec * lightColor;
    
    float3 lighting = (diffuse + specular) * attenuation;
#endif
    
    if (lightType == 1) {
        float spot = spotAttenuation(lightDir, dir, outerConeCos, innerConeCos);
        lighting *= spot;
    }
    
#if defined(PARALLAX)
    float parallaxShadow = getParallaxShadow(parallaxTexture, parallaxSampler, TBN, uv, lightDir, viewDir, height, heightmapScale);
    lighting *= parallaxShadow;
#endif
    
    return lighting * albedo;
}

float luminanceFromColor(float3 color) {
    //standard luminance coefficients
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

//https://64.github.io/tonemapping/
//Interpolates between per-channel reinhard and luninance-based reinhard
float3 reinhardJodie(float3 color) {
    float luminance = luminanceFromColor(color);
    float3 reinhardPerChannel = color / (1.0f + color);
    float3 reinhardLuminance = color / (1.0f + luminance);
    return lerp(reinhardLuminance, reinhardPerChannel, reinhardPerChannel);
}

float3 toneMap_KhronosPbrNeutral(float3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
        return color;

    const float d = 1. - startCompression;
    float newPeak = 1. - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
    return lerp(color, newPeak * float3(1, 1, 1), g);
}

float unprojectDepth(float depth, float near, float far) {
    return near * far / (far - depth * (far - near));
}

float calculatePointShadow(float4 fragPosWorldSpace, int pointLightNum, LightInfo light, StructuredBuffer<float4> pointShadowViewInfoBuffer) {
    float3 lightToFrag = fragPosWorldSpace.xyz-light.posWorldSpace.xyz;
    lightToFrag.z = -lightToFrag.z;
    float3 worldDir = normalize(lightToFrag);

    TextureCube<float> shadowMap = ResourceDescriptorHeap[light.shadowMapIndex];
    SamplerState shadowSampler = SamplerDescriptorHeap[light.shadowSamplerIndex];
    float depthSample = shadowMap.Sample(shadowSampler, worldDir);
    //depthSample = unprojectDepth(depthSample, light.nearPlane, light.farPlane);
    if (depthSample == 1.0) {
        return 0.0;
    }
    
    int faceIndex = 0;
    float maxDir = max(max(abs(worldDir.x), abs(worldDir.y)), abs(worldDir.z));

    if (worldDir.x == maxDir) {
        faceIndex = 0; // +X
    }
    else if (worldDir.x == -maxDir) {
        faceIndex = 1; // -X
    }
    else if (worldDir.y == maxDir) {
        faceIndex = 2; // +Y
    }
    else if (worldDir.y == -maxDir) {
        faceIndex = 3; // -Y
    }
    else if (worldDir.z == maxDir) {
        faceIndex = 4; // +Z
    }
    else if (worldDir.z == -maxDir) {
        faceIndex = 5; // -Z
    }
    matrix lightViewProjectionMatrix = loadMatrixFromBuffer(pointShadowViewInfoBuffer, light.shadowViewInfoIndex*6 + faceIndex);
    
    float4 fragPosLightSpace = mul(float4(fragPosWorldSpace.xyz, 1.0), lightViewProjectionMatrix);
    float dist = length(lightToFrag);
    float lightSpaceDepth = fragPosLightSpace.z / fragPosLightSpace.w;
    
    float shadow = 0.0;
    float bias = 0.0005;
    shadow = lightSpaceDepth - bias > depthSample ? 1.0 : 0.0;
    return shadow;
}

int calculateShadowCascadeIndex(float depth, uint numCascadeSplits, float4 cascadeSplits) {
    for (int i = 0; i < numCascadeSplits; i++) {
        if (depth < cascadeSplits[i]) {
            return i;
        }
    }
    return numCascadeSplits - 1;
}


float calculateCascadedShadow(float4 fragPosWorldSpace, float4 fragPosViewSpace, float3 normal, LightInfo light, uint numCascades, float4 cascadeSplits, StructuredBuffer<float4> cascadeViewBuffer) {
    
    float depth = abs(fragPosViewSpace.z);
    int cascadeIndex = calculateShadowCascadeIndex(depth, numCascades, cascadeSplits);

    int infoIndex = numCascades * light.shadowViewInfoIndex + cascadeIndex;
    matrix lightMatrix = loadMatrixFromBuffer(cascadeViewBuffer, infoIndex);
    float4 fragPosLightSpace = mul(fragPosWorldSpace, lightMatrix);
    float3 uv = fragPosLightSpace.xyz / fragPosLightSpace.w;
    uv.xy = uv.xy * 0.5 + 0.5; // Map to [0, 1] // In OpenGL this would include z, DirectX doesn't need it
    uv.y = 1.0 - uv.y;

    Texture2DArray<float> shadowMap = ResourceDescriptorHeap[light.shadowMapIndex];
    SamplerState shadowSampler = SamplerDescriptorHeap[light.shadowSamplerIndex];
    float closestDepth = shadowMap.Sample(shadowSampler, float3(uv.xy, cascadeIndex)).r;

    float currentDepth = uv.z;
    
    float bias = 0.0008;

    float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
    return shadow;
}

float calculateSpotShadow(float4 fragPosWorldSpace, float3 normal, LightInfo light, matrix lightMatrix) {
    float4 fragPosLightSpace = mul(fragPosWorldSpace, lightMatrix);
    float3 uv = fragPosLightSpace.xyz / fragPosLightSpace.w;
    uv.xy = uv.xy * 0.5 + 0.5; // Map to [0, 1] // In OpenGL this would include z, DirectX doesn't need it
    uv.y = 1.0 - uv.y;
    
    Texture2D<float> shadowMap = ResourceDescriptorHeap[light.shadowMapIndex];
    SamplerState shadowSampler = SamplerDescriptorHeap[light.shadowSamplerIndex];
    float closestDepth = shadowMap.Sample(shadowSampler, uv.xy).r;
    float currentDepth = uv.z;
    //return closestDepth;
    float bias = 0.0008;
    float shadow = currentDepth - bias > closestDepth ? 1.0 : 0.0;
    return shadow;
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
    textureSample.rgb *= 1.0;//0.3; //u_EnvIntensity;
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

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET {

    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[perFrameBuffer.lightBufferIndex];
    ConstantBuffer<MaterialInfo> materialInfo = ResourceDescriptorHeap[materialDataIndex];
    
    float3 viewDir = normalize(perFrameBuffer.eyePosWorldSpace.xyz - input.positionWorldSpace.xyz);
    
    float2 uv = float2(0.0, 0.0);
#if defined(TEXTURED)
    uv = input.texcoord;
    uv*=materialInfo.textureScale;
#endif // TEXTURED
    
#if defined(NORMAL_MAP) || defined(PARALLAX)
    float3x3 TBN = float3x3(input.TBN_T, input.TBN_B, input.TBN_N);
#endif // NORMAL_MAP || PARALLAX
    float height = 0.0;
    
#if defined(PARALLAX)
    Texture2D<float> parallaxTexture = ResourceDescriptorHeap[materialInfo.heightMapIndex];
    SamplerState parallaxSamplerState = SamplerDescriptorHeap[materialInfo.heightSamplerIndex];
    float3 uvh = getContactRefinementParallaxCoordsAndHeight(parallaxTexture, parallaxSamplerState, TBN, uv, viewDir, materialInfo.heightMapScale);
    uv = uvh.xy;
    //height = uvh.z;
#endif // PARALLAX
    
    float4 baseColor = float4(1.0, 1.0, 1.0, 1.0);
    
#if defined(BASE_COLOR_TEXTURE)
    Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[materialInfo.baseColorTextureIndex];
    SamplerState baseColorSamplerState = SamplerDescriptorHeap[materialInfo.baseColorSamplerIndex];
    baseColor = baseColorTexture.Sample(baseColorSamplerState, uv);
    baseColor.rgb = SRGBToLinear(baseColor.rgb);
#endif //BASE_COLOR_TEXTURE
#if defined(PBR)
    baseColor = materialInfo.baseColorFactor * baseColor;
#endif // PBR
    float3 normalWS = input.normalWorldSpace.xyz;
    
#if defined (NORMAL_MAP)
    Texture2D<float4> normalTexture = ResourceDescriptorHeap[materialInfo.normalTextureIndex];
    SamplerState normalSamplerState = SamplerDescriptorHeap[materialInfo.normalSamplerIndex];
    float3 textureNormal = normalTexture.Sample(normalSamplerState, uv).rgb;
    float3 tangentSpaceNormal = normalize(textureNormal * 2.0 - 1.0);
    normalWS = normalize(mul(tangentSpaceNormal, TBN));
#endif // NORMAL_MAP
    
#if defined(DOUBLE_SIDED)
    if(!isFrontFace) {
        normalWS = -normalWS;
    }
#endif // DOUBLE_SIDED
    
    float metallic = 0.0;
    float roughness = 0.0;
    float3 F0 = float3(0.04, 0.04, 0.04); // TODO: this should be specified per-material
    
#if defined(PBR)
    #if defined (PBR_MAPS)
        Texture2D<float4> metallicRoughnessTexture = ResourceDescriptorHeap[materialInfo.metallicRoughnessTextureIndex];
        SamplerState metallicRoughnessSamplerState = SamplerDescriptorHeap[materialInfo.metallicRoughnessSamplerIndex];
        float2 metallicRoughness = metallicRoughnessTexture.Sample(metallicRoughnessSamplerState, uv).gb;
        metallic = metallicRoughness.y * materialInfo.metallicFactor;
        roughness = metallicRoughness.x * materialInfo.roughnessFactor;
    #else
        metallic = materialInfo.metallicFactor;
        roughness = materialInfo.roughnessFactor;
    #endif // PBR_MAPS
#endif // PBR
    F0 = lerp(F0, baseColor.xyz, metallic);
    
#if defined(VERTEX_COLORS)
    baseColor *= input.color;
#endif // VERTEX_COLORS
    
    float3 lighting = float3(0.0, 0.0, 0.0);
    StructuredBuffer<float4> pointShadowViewInfoBuffer = ResourceDescriptorHeap[perFrameBuffer.pointLightCubemapBufferIndex];
    StructuredBuffer<float4> spotShadowViewInfoBuffer = ResourceDescriptorHeap[perFrameBuffer.spotLightMatrixBufferIndex];
    StructuredBuffer<float4> directionalShadowViewInfoBuffer = ResourceDescriptorHeap[perFrameBuffer.directionalLightCascadeBufferIndex];
    
    for (uint i = 0; i < perFrameBuffer.numLights; i++) {
        LightInfo light = lights[i];
        float shadow = 0.0;
        if (light.shadowViewInfoIndex != -1 && light.shadowMapIndex != -1) {
            switch (light.type) {
                case 0:{ // Point light
                        shadow = calculatePointShadow(input.positionWorldSpace, i, light, pointShadowViewInfoBuffer);
                        //return float4(shadow, shadow, shadow, 1.0);
                        break;
                    }
                case 1:{ // Spot light
                        matrix lightMatrix = loadMatrixFromBuffer(spotShadowViewInfoBuffer, light.shadowViewInfoIndex);
                        shadow = calculateSpotShadow(input.positionWorldSpace, normalWS, light, lightMatrix);
                        break;
                    }
                case 2:{// Directional light
                        shadow = calculateCascadedShadow(input.positionWorldSpace, input.positionViewSpace, normalWS, light, perFrameBuffer.numShadowCascades, perFrameBuffer.shadowCascadeSplits, directionalShadowViewInfoBuffer);
                        break;
                    }
            }
        }
#if defined (PARALLAX)
        lighting += (1.0 - shadow) * calculateLightContribution(light, input.positionWorldSpace.xyz, viewDir, normalWS, uv, baseColor.xyz, metallic, roughness, F0, height, parallaxTexture, parallaxSamplerState, TBN, materialInfo.heightMapScale);
#else
        lighting += (1.0 - shadow) * calculateLightContribution(light, input.positionWorldSpace.xyz, viewDir, normalWS, uv, baseColor.xyz, metallic, roughness, F0);
#endif
    }
    float ao = 1.0;
#if defined(AO_TEXTURE)
    Texture2D<float4> aoTexture = ResourceDescriptorHeap[materialInfo.aoMapIndex];
    SamplerState aoSamplerState = SamplerDescriptorHeap[materialInfo.aoSamplerIndex];
    ao = aoTexture.Sample(aoSamplerState, uv).r;
#endif
    
  #if defined(IMAGE_BASED_LIGHTING)
  
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
    float3 f_metal_fresnel_ibl = getIBLGGXFresnel(normalWS, viewDir, roughness, baseColor.rgb, 1.0, brdfLUT, brdfSampler);
    float3 f_metal_brdf_ibl = f_metal_fresnel_ibl * f_specular_metal;
    
    // Dielectric fresnel
    float specularWeight = 1.0;
    float3 f_dielectric_fresnel_ibl = getIBLGGXFresnel(normalWS, viewDir, roughness, baseColor.rgb, specularWeight, brdfLUT, brdfSampler);
    float3 f_dielectric_brdf_ibl = lerp(f_diffuse, f_specular_dielectric, f_dielectric_fresnel_ibl);
    
    float3 ambient = lerp(f_dielectric_brdf_ibl, f_metal_brdf_ibl, metallic);
    
    //return float4(lighting, 1.0);
    
#else 
    float3 ambient = perFrameBuffer.ambientLighting.xyz * baseColor.xyz * ao;
#endif // IMAGE_BASED_LIGHTING
    
    lighting += ambient;
#if defined(VERTEX_COLORS)
    lighting = lighting * input.color.xyz;
#endif // VERTEX_COLORS
    
#if defined(EMISSIVE_TEXTURE)
    Texture2D<float4> emissiveTexture = ResourceDescriptorHeap[materialInfo.emissiveTextureIndex];
    SamplerState emissiveSamplerState = SamplerDescriptorHeap[materialInfo.emissiveSamplerIndex];
    lighting += SRGBToLinear(emissiveTexture.Sample(emissiveSamplerState, uv).rgb)*materialInfo.emissiveFactor.rgb;
#else
    lighting += materialInfo.emissiveFactor.rgb;
#endif
    
    // Reinhard tonemapping
    lighting = reinhardJodie(lighting);
    //lighting = toneMap_KhronosPbrNeutral(lighting);
#if defined(PBR)
    // Gamma correction
    lighting = LinearToSRGB(lighting);
#endif
        
    float opacity = baseColor.a;
    return float4(lighting, opacity);
}