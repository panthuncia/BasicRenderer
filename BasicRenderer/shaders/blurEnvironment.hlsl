cbuffer PrefilterPC : register(b0, space0)
{
    uint SrcCubeSrvIndex;
    uint DstCubeUavIndex;
    uint Face;
    uint Size;
    uint RoughnessBits; // asuint(roughness)
};

SamplerState gLinearClamp : register(s0);

static const float PI = 3.14159265359;

// Face uv [-1,1] -> world dir
float3 FaceUVToDir(uint face, float2 uv)
{
    switch (face)
    {
        case 0:
            return normalize(float3(1.0, uv.y, -uv.x)); // +X
        case 1:
            return normalize(float3(-1.0, uv.y, uv.x)); // -X
        case 2:
            return normalize(float3(uv.x, 1.0, -uv.y)); // +Y
        case 3:
            return normalize(float3(uv.x, -1.0, uv.y)); // -Y
        case 4:
            return normalize(float3(uv.x, uv.y, 1.0)); // +Z
        default:
            return normalize(float3(-uv.x, uv.y, -1.0)); // -Z
    }
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

uint ReverseBits(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return bits;
}

float RadicalInverse_VdC(uint bits)
{
    return float(ReverseBits(bits)) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= Size || tid.y >= Size)
        return;

    float roughness = asfloat(RoughnessBits);

    // Compute cube face direction for this pixel
    float2 uv = (float2(tid.xy) + 0.5) / float(Size); // [0,1]
    uv = uv * 2.0 - 1.0; // [-1,1]
    float3 N = normalize(FaceUVToDir(Face, uv));
    float3 V = N;

    TextureCube<float4> srcCube = ResourceDescriptorHeap[SrcCubeSrvIndex];
    RWTexture2DArray<float4> dstCube = ResourceDescriptorHeap[DstCubeUavIndex];

    const uint SAMPLE_COUNT = 16u;
    float3 prefiltered = 0.0;
    float totalWeight = 0.0;

    [loop]
    for (uint i = 0; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float ndotl = max(dot(N, L), 0.0);

        if (ndotl > 0.0)
        {
            float3 c = srcCube.Sample(gLinearClamp, L).rgb;
            prefiltered += c * ndotl;
            totalWeight += ndotl;
        }
    }

    prefiltered = (totalWeight > 0.0) ? prefiltered / totalWeight : 0.0;
    dstCube[uint3(tid.x, tid.y, Face)] = float4(prefiltered, 1.0);
}