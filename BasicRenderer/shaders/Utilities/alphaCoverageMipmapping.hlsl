#include "include/cbuffers.hlsli"

static const uint ALPHA_MIP_FLAG_SRGB = 1u;
static const float ALPHA_COVERAGE_CUTOFF = 0.5f;
static const float ALPHA_COVERAGE_SCALE_TARGET = 0.55f;
static const uint ALPHA_MIP_STATS_WORDS = 260u;
static const uint ALPHA_MIP_PREV_COVERED = 0u;
static const uint ALPHA_MIP_PREV_TOTAL = 1u;
static const uint ALPHA_MIP_DST_TOTAL = 2u;
static const uint ALPHA_MIP_HIST_BASE = 3u;

uint SrcWidth() { return UintRootConstant4; }
uint SrcHeight() { return UintRootConstant5; }
uint DstWidth() { return UintRootConstant6; }
uint DstHeight() { return UintRootConstant7; }
uint StatsBase() { return UintRootConstant8; }
uint ScaleIndex() { return UintRootConstant9; }
uint Flags() { return UintRootConstant10; }

float3 LinearToSrgb(float3 c)
{
    c = saturate(c);
    float3 lo = c * 12.92f;
    float3 hi = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
    return lerp(lo, hi, step(0.0031308f, c));
}

float3 SrgbToLinear(float3 c)
{
    c = saturate(c);
    float3 lo = c / 12.92f;
    float3 hi = pow((c + 0.055f) / 1.055f, 2.4f);
    return lerp(lo, hi, step(0.04045f, c));
}

float4 EncodeForOutput(float4 v)
{
    if ((Flags() & ALPHA_MIP_FLAG_SRGB) != 0u)
    {
        v.rgb = LinearToSrgb(v.rgb);
    }
    return saturate(v);
}

float4 LoadSource(uint2 p)
{
    p = min(p, uint2(max(1u, SrcWidth()) - 1u, max(1u, SrcHeight()) - 1u));
    Texture2D<float4> srcTexture = ResourceDescriptorHeap[UintRootConstant0];
    float4 value = srcTexture.Load(int3(p, 0));
    if ((Flags() & ALPHA_MIP_FLAG_SRGB) != 0u)
    {
        value.rgb = SrgbToLinear(value.rgb);
    }
    return value;
}

float3 DilatedRgb(uint2 p)
{
    float4 center = LoadSource(p);
    if (center.a >= ALPHA_COVERAGE_CUTOFF)
    {
        return center.rgb;
    }

    float bestAlpha = center.a;
    float3 bestRgb = center.rgb;

    [unroll]
    for (int dy = -2; dy <= 2; ++dy)
    {
        [unroll]
        for (int dx = -2; dx <= 2; ++dx)
        {
            int2 q = int2(p) + int2(dx, dy);
            q = clamp(q, int2(0, 0), int2(max(1u, SrcWidth()) - 1u, max(1u, SrcHeight()) - 1u));
            Texture2D<float4> srcTexture = ResourceDescriptorHeap[UintRootConstant0];
            float4 sampleValue = srcTexture.Load(int3(q, 0));
            if ((Flags() & ALPHA_MIP_FLAG_SRGB) != 0u)
            {
                sampleValue.rgb = SrgbToLinear(sampleValue.rgb);
            }
            if (sampleValue.a > bestAlpha)
            {
                bestAlpha = sampleValue.a;
                bestRgb = sampleValue.rgb;
            }
            if (sampleValue.a >= ALPHA_COVERAGE_CUTOFF)
            {
                return sampleValue.rgb;
            }
        }
    }

    return bestRgb;
}

[numthreads(256, 1, 1)]
void AlphaMipResetStatsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= ALPHA_MIP_STATS_WORDS)
    {
        return;
    }

    RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[UintRootConstant2];
    RWStructuredBuffer<float> scales = ResourceDescriptorHeap[UintRootConstant3];
    stats[StatsBase() + dispatchThreadId.x] = 0u;
    if (dispatchThreadId.x == 0u)
    {
        scales[ScaleIndex()] = 1.0f;
    }
}

[numthreads(8, 8, 1)]
void AlphaMipDownsampleCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 dst = dispatchThreadId.xy;
    if (dst.x >= DstWidth() || dst.y >= DstHeight())
    {
        return;
    }

    const uint2 srcBase = dst * 2u;
    RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[UintRootConstant2];
    RWTexture2D<float4> dstTexture = ResourceDescriptorHeap[UintRootConstant1];
    float3 rgbSum = 0.0f;
    float alphaSum = 0.0f;

    [unroll]
    for (uint oy = 0u; oy < 2u; ++oy)
    {
        [unroll]
        for (uint ox = 0u; ox < 2u; ++ox)
        {
            const uint2 src = srcBase + uint2(ox, oy);
            const bool inBounds = src.x < SrcWidth() && src.y < SrcHeight();
            float4 sampleValue = LoadSource(src);
            rgbSum += DilatedRgb(src);
            alphaSum += sampleValue.a;

            if (inBounds)
            {
                InterlockedAdd(stats[StatsBase() + ALPHA_MIP_PREV_TOTAL], 1u);
                if (sampleValue.a >= ALPHA_COVERAGE_CUTOFF)
                {
                    InterlockedAdd(stats[StatsBase() + ALPHA_MIP_PREV_COVERED], 1u);
                }
            }
        }
    }

    float4 outValue = float4(rgbSum * 0.25f, alphaSum * 0.25f);
    dstTexture[dst] = EncodeForOutput(outValue);

    const uint bin = min(255u, (uint)round(saturate(outValue.a) * 255.0f));
    InterlockedAdd(stats[StatsBase() + ALPHA_MIP_DST_TOTAL], 1u);
    InterlockedAdd(stats[StatsBase() + ALPHA_MIP_HIST_BASE + bin], 1u);
}

[numthreads(1, 1, 1)]
void AlphaMipResolveScaleCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    RWStructuredBuffer<uint> stats = ResourceDescriptorHeap[UintRootConstant2];
    RWStructuredBuffer<float> scales = ResourceDescriptorHeap[UintRootConstant3];
    const uint prevTotal = stats[StatsBase() + ALPHA_MIP_PREV_TOTAL];
    const uint prevCovered = stats[StatsBase() + ALPHA_MIP_PREV_COVERED];
    const uint dstTotal = stats[StatsBase() + ALPHA_MIP_DST_TOTAL];

    if (prevTotal == 0u || dstTotal == 0u || prevCovered == 0u)
    {
        scales[ScaleIndex()] = 1.0f;
        return;
    }

    const float targetCoverage = (float)prevCovered / (float)prevTotal;
    const uint targetCovered = min(dstTotal, max(1u, (uint)round(targetCoverage * (float)dstTotal)));

    uint accumulated = 0u;
    uint selectedBin = 255u;
    for (int bin = 255; bin >= 1; --bin)
    {
        accumulated += stats[StatsBase() + ALPHA_MIP_HIST_BASE + (uint)bin];
        if (accumulated >= targetCovered)
        {
            selectedBin = (uint)bin;
            break;
        }
    }

    const float selectedAlpha = max((float)selectedBin / 255.0f, 1.0f / 255.0f);
    scales[ScaleIndex()] = clamp(ALPHA_COVERAGE_SCALE_TARGET / selectedAlpha, 0.25f, 16.0f);
}

[numthreads(8, 8, 1)]
void AlphaMipApplyScaleCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint2 dst = dispatchThreadId.xy;
    if (dst.x >= DstWidth() || dst.y >= DstHeight())
    {
        return;
    }

    RWTexture2D<float4> dstTexture = ResourceDescriptorHeap[UintRootConstant1];
    RWStructuredBuffer<float> scales = ResourceDescriptorHeap[UintRootConstant3];
    float4 value = dstTexture[dst];
    value.a = saturate(value.a * scales[ScaleIndex()]);
    dstTexture[dst] = value;
}
