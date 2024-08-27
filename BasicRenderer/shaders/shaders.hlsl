struct PerFrameBuffer {
    row_major matrix view;
    row_major matrix projection;
    float3 eyePosWorldSpace;
};

cbuffer PerMesh : register(b1) {
    row_major matrix model;
};

struct LightInfo {
    int4 properties;
    float4 posWorldSpace;
    float4 dirWorldSpace;
    float4 attenuation;
    float4 color;
};

struct MaterialInfo {
    uint psoFlags;
    uint baseColorTextureIndex;
    uint normalTextureIndex;
    uint metallicRoughnessTextureIndex;
    uint emissiveTextureIndex;
    uint aoMapIndex;
    uint heightMapIndex;
    float metallicFactor;
    float roughnessFactor;
    float ambientStrength;
    float specularStrength;
    float textureScale;
    float heightMapScale;
    float4 baseColorFactor;
    float4 emissiveFactor;
};



struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
#if defined(VERTEX_COLORS)
    float4 color : COLOR;
#endif
};

struct PSInput {
    float4 position : SV_POSITION;
    float4 positionWorldSpace : TEXCOORD1;
    float4 normalWorldSpace : TEXCOORD2;
#if defined(VERTEX_COLORS)
    float4 color : COLOR;
#endif
};

PSInput VSMain(VSInput input) {
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    
    PSInput output;
    float4 worldPosition = mul(float4(input.position, 1.0f), model);
    float4 viewPosition = mul(worldPosition, perFrameBuffer.view);
    output.normalWorldSpace = mul(float4(input.normal, 1.0f), model);
    output.positionWorldSpace = worldPosition;
    output.position = mul(viewPosition, perFrameBuffer.projection);
#if defined(VERTEX_COLORS)
    output.color = input.color;
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

float3 calculateLightContribution(LightInfo light, float3 fragPos, float3 viewDir, float3 normal, float3 albedo) {
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
    
    float diff = max(dot(normal, lightDir), 0.0);
    float3 diffuse = diff * lightColor;

        // Calculate specular light, Blinn-Phong
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 32.0);
    float3 specular = /*u_specularStrength * */ spec * lightColor;
    
    float3 lighting = (diffuse + specular) * attenuation;

    if (lightType == 1) {
        float spot = spotAttenuation(lightDir, dir, outerConeCos, innerConeCos);
        lighting *= spot;
    }
    return lighting;
}

float4 PSMain(PSInput input) : SV_TARGET {
    
    ConstantBuffer<PerFrameBuffer> perFrameBuffer = ResourceDescriptorHeap[0];
    StructuredBuffer<LightInfo> lights = ResourceDescriptorHeap[1];
    
    float3 viewDir = normalize(perFrameBuffer.eyePosWorldSpace - input.positionWorldSpace.xyz);
    uint numLights, lightsStride;
    lights.GetDimensions(numLights, lightsStride);

    float3 baseColor = float3(1.0, 1.0, 1.0);
#if defined(VERTEX_COLORS)
    baseColor *= input.color.xyz;
#endif
    
    float3 lighting = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < numLights; i++) {
        LightInfo light = lights[i];
        lighting += calculateLightContribution(light, input.positionWorldSpace.xyz, viewDir, input.normalWorldSpace.xyz, baseColor);
    }
    
#if defined(VERTEX_COLORS)
    lighting = lighting * input.color.xyz;
#endif
    
    return float4(lighting, 1.0);
}