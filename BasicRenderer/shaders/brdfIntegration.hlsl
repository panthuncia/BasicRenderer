struct VS_OUTPUT {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD1;
};

VS_OUTPUT VSMain(float3 pos : POSITION, float2 uv : TEXCOORD0) {
    VS_OUTPUT output;
    output.position = float4(pos, 1.0);
    output.uv = uv;
    return output;
}

// https://learnopengl.com/PBR/IBL/Specular-IBL
static const float PI = 3.14159265359;
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
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

float2 hammersley(uint i, float numSamples)
{
    uint bits = i;
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
    return float2(i / numSamples, bits / exp2(32));
}

float GDFG(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
    float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
    return (2 * NoL) / (GGXV + GGXL);
}

float2 IntegrateBRDF(float NoV, float a) {
    const uint sampleCount = 1024u;
    const float3 N = float3(0.0f, 0.0f, 1.0f); // Normal vector
    float3 V;
    V.x = sqrt(1.0f - NoV * NoV);
    V.y = 0.0f;
    V.z = NoV;

    float2 r = 0.0f;
    for (uint i = 0; i < sampleCount; i++)
    {
        float2 Xi = hammersley(i, sampleCount);
        float3 H = ImportanceSampleGGX(Xi, N, a);
        float3 L = 2.0f * dot(V, H) * H - V;

        float VoH = saturate(dot(V, H));
        float NoL = saturate(L.z);
        float NoH = saturate(H.z);

        if (NoL > 0.0f)
        {
            float G = GDFG(NoV, NoL, a);
            float Gv = G * VoH / NoH;
            float Fc = pow(1 - VoH, 5.0f);
            r.x += Gv * Fc;
            r.y += Gv;
        }
    }
    return r * (1.0f / sampleCount);
    
    
    
}

float2 PSMain(VS_OUTPUT input) : SV_TARGET {
    float2 integratedBRDF = IntegrateBRDF(input.uv.x, 1.0-input.uv.y);
    return integratedBRDF;
}