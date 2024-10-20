#if defined(VERTEX_COLORS)
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
};
#else
struct Vertex {
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