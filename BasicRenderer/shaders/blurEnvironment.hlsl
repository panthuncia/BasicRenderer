cbuffer RootConstants1 : register(b1) {
    matrix viewProjectionMatrix;
};

cbuffer RootConstants1 : register(b2) {
    float roughness;
};

Texture2D environmentTexture : register(t0);
SamplerState environmentSamplerState : register(s0);

struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD1;
};

VS_OUTPUT VSMain(float3 pos : POSITION) {
    VS_OUTPUT output;
    output.direction = normalize(pos);
    output.position = mul(viewProjectionMatrix, float4(pos, 1.0f));
    output.position.z = output.position.w - 0.00001;
    return output;
}

static const float2 invAtan = float2(0.1591, 0.3183);
static const float PI = 3.14159265359;
// Converts a direction vector to u,v coordinates on an equirectangular map
float2 CubeToSpherical(float3 dir) {
    dir = normalize(dir);

    float2 uv;
    uv.x = (atan2(dir.z, dir.x) / (2.0 * PI)) + 0.5;
    uv.y = (asin(dir.y) / PI) + 0.5;
    uv.y = 1.0 - uv.y;
    return uv;
}

struct PS_OUTPUT {
    float4 prefilteredColor : SV_Target0;
};

//https://learnopengl.com/PBR/IBL/Specular-IBL
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;
	
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
	
    // from spherical coordinates to cartesian coordinates
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;
	
    // from tangent-space vector to world-space sample vector
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
	
    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

PS_OUTPUT PSMain(VS_OUTPUT input) {
    
    float3 N = normalize(input.direction.xyz);
    float3 R = N;
    float3 V = R;
    
    const uint SAMPLE_COUNT = 16u;
    float totalWeight = 0.0;
    float3 prefilteredColor = float3(0.0, 0.0, 0.0);
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        
        float ndotL = max(dot(N, L), 0.0);
        if (ndotL > 0.0) {
            prefilteredColor += environmentTexture.Sample(environmentSamplerState, CubeToSpherical(L)).rgb * ndotL;
            totalWeight += ndotL;
        }
    }
    
    prefilteredColor = prefilteredColor / totalWeight;
    
    
    PS_OUTPUT output;
    output.prefilteredColor = float4(prefilteredColor, 1.0);
    return output;
}