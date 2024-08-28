#define PI 3.1415926538

struct PerFrameBuffer {
    row_major matrix view;
    row_major matrix projection;
    float3 eyePosWorldSpace;
};

cbuffer PerObject : register(b1) {
    row_major matrix model;
    row_major float3x3 normalMatrix;
};

cbuffer PerMesh : register(b2) {
    uint materialDataIndex;
};

struct LightInfo {
    // Light attributes: x=type (0=point, 1=spot, 2=directional)
    // x=point -> w = shadow caster
    // x=spot -> y= inner cone angle, z= outer cone angle, w= shadow caster
    // x=directional => w= shadow caster
    int4 properties;
    float4 posWorldSpace; // Position of the lights
    float4 dirWorldSpace; // Direction of the lights
    float4 attenuation; // x,y,z = constant, linear, quadratic attenuation, w= max range
    float4 color; // Color of the lights
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
    float4 baseColorFactor;
    float4 emissiveFactor;
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
};
#endif

#if defined(VERTEX_COLORS)
struct PSInput {
    float4 position : SV_POSITION;
    float4 positionWorldSpace : TEXCOORD1;
    float4 normalWorldSpace : TEXCOORD2;
    float4 color : COLOR;
};
#else
struct PSInput {
    float4 position : SV_POSITION;
    float4 positionWorldSpace : TEXCOORD1;
    float3 normalWorldSpace : TEXCOORD2;
#if defined(TEXTURED)
    float2 texcoord : TEXCOORD0;
#endif
};
#endif

PSInput VSMain(VSInput input) {
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    
    PSInput output;
    float4 worldPosition = mul(float4(input.position, 1.0f), model);
    float4 viewPosition = mul(worldPosition, perFrameBuffer.view);
    output.normalWorldSpace = mul(input.normal, normalMatrix);
    output.positionWorldSpace = worldPosition;
    output.position = mul(viewPosition, perFrameBuffer.projection);
#if defined(VERTEX_COLORS)
    output.color = input.color;
#endif
#if defined(TEXTURED)
    output.texcoord = input.texcoord;
#endif
    return output;
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

float3 calculateLightContribution(LightInfo light, float3 fragPos, float3 viewDir, float3 normal, float2 uv, float3 albedo, float metallic, float roughness, float3 F0) {
    uint lightType = light.properties.x;
    float3 lightPos = light.posWorldSpace.xyz;
    float3 lightColor = light.color.xyz;
    float3 dir = light.dirWorldSpace.xyz;
    float constantAttenuation = light.attenuation.x;
    float linearAttenuation = light.attenuation.y;
    float quadraticAttenuation = light.attenuation.z;
    
    float outerConeCos = light.properties.z;
    float innerConeCos = light.properties.y;
    
    float3 lightDir;
    float distance;
    float attenuation;
    float spotAttenuationFactor = 0;
    
    // For directional lights, use light dir directly, with zero attenuation
    if (lightType == 2) {
        lightDir = dir;
        attenuation = 1.0;
    } else {
        lightDir = normalize(lightPos - fragPos);
        distance = length(lightPos - fragPos);
        attenuation = 1.0 / (constantAttenuation + linearAttenuation * distance + quadraticAttenuation * distance * distance);
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
    return lighting * albedo;
}

float4 PSMain(PSInput input) : SV_TARGET {
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[1];
    
    float3 viewDir = normalize(perFrameBuffer.eyePosWorldSpace - input.positionWorldSpace.xyz);
    uint numLights, lightsStride;
    lights.GetDimensions(numLights, lightsStride);

    float3 baseColor = float3(1.0, 1.0, 1.0);
    ConstantBuffer<MaterialInfo> materialInfo = ResourceDescriptorHeap[materialDataIndex];
    
    float2 uv = float2(0.0, 0.0);
#if defined(TEXTURED)
    uv = input.texcoord;
#endif // TEXTURED
#if defined(BASE_COLOR_TEXTURE)
    Texture2D<float4> baseColorTexture = ResourceDescriptorHeap[materialInfo.baseColorTextureIndex];
    SamplerState baseColorSamplerState = SamplerDescriptorHeap[materialInfo.baseColorSamplerIndex];
    baseColor = baseColorTexture.Sample(baseColorSamplerState, uv).rgb;
    #if defined(PBR)
        baseColor *= materialInfo.baseColorFactor.xyz;
    #endif // PBR
#endif //BASE_COLOR_TEXTURE
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
    baseColor *= input.color.xyz;
#endif // VERTEX_COLORS
    
    float3 lighting = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < numLights; i++) {
        LightInfo light = lights[i];
        lighting += calculateLightContribution(light, input.positionWorldSpace.xyz, viewDir, input.normalWorldSpace, uv, baseColor, metallic, roughness, F0);
    }
    
#if defined(VERTEX_COLORS)
    lighting = lighting * input.color.xyz;
#endif
    
    return float4(lighting, 1.0);
}