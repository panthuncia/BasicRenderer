cbuffer RootConstants1 : register(b1) {
    matrix viewProjectionMatrix;
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
    output.position.z = output.position.w-0.00001;
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
    float4 color : SV_Target0;
};

PS_OUTPUT PSMain(VS_OUTPUT input) {
    float3 color = environmentTexture.Sample(environmentSamplerState, CubeToSpherical(input.direction.xyz)).xyz;
    
    PS_OUTPUT output;
    output.color = float4(color, 1.0);
    return output;
}