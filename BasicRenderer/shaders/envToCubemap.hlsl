cbuffer EnvToCubePC : register(b0, space0)
{
    uint SrcEnvSrvIndex; // SRV index in bindless CBV/SRV/UAV heap
    uint DstFaceUavIndex; // UAV index for the DEST cubemap face (arraySize=1 view)
    uint Face; // 0..5
    uint Size; // cubemap resolution (width=height)
};

SamplerState gLinearClamp : register(s0);

static const float PI = 3.14159265359;
float2 DirToEquirect(float3 dir)
{
    dir = normalize(dir);
    float2 uv;
    uv.x = (atan2(dir.z, dir.x) / (2.0 * PI)) + 0.5;
    uv.y = 0.5 - (asin(dir.y) / PI);
    return uv;
}

float3 FaceUVToDir(uint face, float2 uv)
{
    // uv in [-1,1], +X,-X,+Y,-Y,+Z,-Z
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

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= Size || tid.y >= Size)
        return;

    float2 uv = ((float2) tid.xy + 0.5) / float(Size); // [0,1]
    uv = uv * 2.0 - 1.0; // [-1,1]
    float3 dir = FaceUVToDir(Face, uv);

    float2 equirect = DirToEquirect(dir);
    Texture2D<float4> srcTex = ResourceDescriptorHeap[SrcEnvSrvIndex];
    float3 col = srcTex.Sample(gLinearClamp, equirect).xyz;
    
    RWTexture2D<float4> dstTex = ResourceDescriptorHeap[DstFaceUavIndex];
    dstTex[int2(tid.xy)] = float4(col, 1.0);
}