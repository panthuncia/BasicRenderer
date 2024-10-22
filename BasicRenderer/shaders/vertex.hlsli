#if defined(PSO_VERTEX_COLORS)
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
};
#else
struct Vertex {
    float3 position : POSITION;
    float3 normal : NORMAL;
#if defined(PSO_TEXTURED)
    float2 texcoord : TEXCOORD0;
#endif
#if defined(PSO_NORMAL_MAP) || defined(PSO_PARALLAX)
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
#endif // NORMAL_MAP
#if defined(PSO_SKINNED)
    uint4 joints : TEXCOORD1;
    float4 weights : TEXCOORD2;
#endif // SKINNED
};
#endif

// Vertex struct for mesh shaders
// manually assembled from ByteAddressBuffer
struct MeshVertex {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
    uint4 joints : TEXCOORD1;
    float4 weights : TEXCOORD2;
};

#if defined(PSO_VERTEX_COLORS)
struct PSInput {
    float4 position : SV_POSITION;
    float4 positionWorldSpace : TEXCOORD3;
    float4 positionViewSpace : TEXCOORD4;
    float3 normalWorldSpace : TEXCOORD5;
    float4 color : COLOR;
    uint meshletIndex : TEXCOORD6;
};
#else
struct PSInput {
    float4 position : SV_POSITION;
    float4 positionWorldSpace : TEXCOORD3;
    float4 positionViewSpace : TEXCOORD4;
    float3 normalWorldSpace : TEXCOORD5;
#if defined(PSO_TEXTURED)
    float2 texcoord : TEXCOORD0;
#endif // TEXTURED
#if defined(PSO_NORMAL_MAP) || defined(PSO_PARALLAX)
    float3 TBN_T : TEXCOORD6;      // First row of TBN
    float3 TBN_B : TEXCOORD7;      // Second row of TBN
    float3 TBN_N : TEXCOORD8;      // Third row of TBN
#endif // NORMAL_MAP
    uint meshletIndex : TEXCOORD9;
};
#endif

#define VERTEX_COLORS 1 << 0
#define VERTEX_NORMAL 1 << 1
#define VERTEX_TEXCOORDS 1 << 2
#define VERTEX_SKINNED 1 << 3
#define VERTEX_TANBIT 1 << 4