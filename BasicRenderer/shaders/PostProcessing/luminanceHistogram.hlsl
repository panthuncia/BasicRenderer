#include "include/cbuffers.hlsli"
#include "PerPassRootConstants/luminanceHistogramRootConstants.h"

// https://bruop.github.io/exposure/
static const uint GROUP_SIZE = 256;
static const float EPSILON = 0.005f;
static const float3 RGB_TO_LUM = float3(0.2125f, 0.7154f, 0.0721f);

// Shared memory for per-group histogram
groupshared uint histogramShared[GROUP_SIZE];

// Convert an HDR RGB -> luminance bin index [0..255]
uint colorToBin(float3 hdrColor, float minLogLum, float inverseLogLumRange)
{
    float lum = dot(hdrColor, RGB_TO_LUM);

    // bin 0 for almost black
    if (lum < EPSILON)
        return 0u;

    // map log2(lum) into [0,1]
    float logLum = saturate((log2(lum) - minLogLum) * inverseLogLumRange);

    // bin [1..255]
    return (uint) (logLum * 254.0f + 1.0f);
}

[numthreads(16, 16, 1)]
void CSMain(
    uint3 dispatchThreadID : SV_DispatchThreadID, // global coords
    uint3 groupThreadID : SV_GroupThreadID, // local 2D coords
    uint groupIndex : SV_GroupIndex // linear local index [0..255]
)
{
    // Zero out our local-bin
    histogramShared[groupIndex] = 0;
    GroupMemoryBarrierWithGroupSync();

    Texture2D<float4> s_texColor = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::Color::HDRColorTarget)];
    RWStructuredBuffer<uint> histogram = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostProcessing::LuminanceHistogram)];
    
    // If inside image bounds, compute this pixel's bin and atomically increment local
    uint width, height;
    s_texColor.GetDimensions(width, height);
    if (dispatchThreadID.x < width && dispatchThreadID.y < height)
    {
        float3 hdr = s_texColor.Load(int3(dispatchThreadID.xy, 0)).xyz;
        uint bin = colorToBin(hdr, MIN_LOG_LUMINANCE, INVERSE_LOG_LUM_RANGE);
        uint unused;
        InterlockedAdd(histogramShared[bin], 1, unused);
    }

    // Wait for all lanes, then push local bins to the global histogram
    GroupMemoryBarrierWithGroupSync();

    {
        uint unused;
        //histogram.InterlockedAdd(groupIndex, histogramShared[groupIndex], unused);
        InterlockedAdd(histogram[groupIndex], histogramShared[groupIndex], unused);
    }
}
