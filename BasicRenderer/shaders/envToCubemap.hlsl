cbuffer RootConstants1 : register(b1) {
    matrix viewProjectionMatrix;
};

/*cbuffer RootConstants2 : register(b2) {
    uint skyboxTextureIndex;
};

cbuffer RootConstants1 : register(b3) {
    uint skyboxSamplerIndex;
};*/

Texture2D environmentTexture : register(t0);
SamplerState environmentSamplerState : register(s0);

// Vertex Shader
struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD1;
};

VS_OUTPUT VSMain(float3 pos : POSITION) {
    VS_OUTPUT output;
    output.direction = normalize(pos);
    output.position = mul(viewProjectionMatrix, float4(pos, 1.0f));
    output.position.z = output.position.w-0.00001;
    return output;
}

static const float2 invAtan = float2(0.1591, 0.3183);
float2 CubeToSpherical(float3 v) {
    float2 uv = float2(atan2(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

struct PS_OUTPUT {
    float4 color : SV_Target0;
    float4 radiance : SV_Target1;
};

static const float PI = 3.14159265359;

PS_OUTPUT PSMain(VS_OUTPUT input) {
    float3 color = environmentTexture.Sample(environmentSamplerState, CubeToSpherical(input.direction.xyz)).xyz;
    
    float3 irradiance = float3(0.0, 0.0, 0.0);
    float3 normal = input.direction.xyz;
    float3 up = float3(0.0, 1.0, 0.0);
    float3 right = normalize(cross(up, normal));
    up = normalize(cross(normal, right));

    float sampleDelta = 0.025;
    float nrSamples = 0.0;
    // https://learnopengl.com/PBR/IBL/Diffuse-irradiance
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            // spherical to cartesian (in tangent space)
            float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            float3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal;

            irradiance += environmentTexture.Sample(environmentState, CubeToSpherical(sampleVec)).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / float(nrSamples));
    
    PS_OUTPUT output;
    output.color = float4(color, 1.0);
    output.radiance = float4(irradiance, 1.0);
    return output;
}