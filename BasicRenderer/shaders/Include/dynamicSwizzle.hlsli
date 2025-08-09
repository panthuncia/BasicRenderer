#ifndef __DYNAMIC_SWIZZLE_HLSLI__
#define __DYNAMIC_SWIZZLE_HLSLI__

// ============================================================
// 1-component extract (always returns a scalar)
// idx0 = 0...3
float DynamicSwizzle(float2 v, uint idx)
{
    return v[idx];
}
float DynamicSwizzle(float3 v, uint idx)
{
    return v[idx];
}
float DynamicSwizzle(float4 v, uint idx)
{
    return v[idx];
}

// ============================================================
// 2-component extract (returns float2)
// idx0, idx1 = 0...3
float2 DynamicSwizzle(float v, uint2 idx)
{
    float x = v;
    float y = v;
    return float2(x, y);
}
float2 DynamicSwizzle(float2 v, uint2 idx)
{
    return float2(v[idx.x], v[idx.y]);

}
float2 DynamicSwizzle(float3 v, uint2 idx)
{
    return float2(v[idx.x], v[idx.y]);

}
float2 DynamicSwizzle(float4 v, uint2 idx)
{
    return float2(v[idx.x], v[idx.y]);
}

// ============================================================
// 3-component extract (returns float3)
// idx0, idx1, idx2 = 0...3
float3 DynamicSwizzle(float v, uint3 idx)
{
    return float3(v, v, v);
}
float3 DynamicSwizzle(float2 v, uint3 idx)
{
    return float3(v[idx.x], v[idx.y], v[idx.z]);
}
float3 DynamicSwizzle(float3 v, uint3 idx)
{
    return float3(v[idx.x], v[idx.y], v[idx.z]);
}
float3 DynamicSwizzle(float4 v, uint3 idx)
{
    return float3(v[idx.x], v[idx.y], v[idx.z]);
}

// ============================================================
// 4-component extract (returns float4)
// idx0, idx1, idx2, idx3 = 0...3
float4 DynamicSwizzle(float v, uint4 idx)
{
    return float4(v, v, v, v);
}
float4 DynamicSwizzle(float2 v, uint4 idx)
{
    return float4(v[idx.x], v[idx.y], v[idx.z], v[idx.w]);
}
float4 DynamicSwizzle(float3 v, uint4 idx)
{
    return float4(v[idx.x], v[idx.y], v[idx.z], v[idx.w]);
}
float4 DynamicSwizzle(float4 v, uint4 idx)
{
    return float4(v[idx.x], v[idx.y], v[idx.z], v[idx.w]);
}

#endif // __DYNAMIC_SWIZZLE_HLSLI__