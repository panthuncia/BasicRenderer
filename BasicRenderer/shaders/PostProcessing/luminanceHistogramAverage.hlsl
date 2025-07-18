#include "include/cbuffers.hlsli"
#include "PerPassRootConstants/luminanceHistogramAverageRootConstants.h"

// https://bruop.github.io/exposure/
static const uint GROUP_SIZE = 256;
static const uint THREADS_X = 256;
static const uint THREADS_Y = 1;

groupshared uint histogramShared[GROUP_SIZE];

[numthreads(THREADS_X, THREADS_Y, 1)]
void CSMain(
    uint3 dispatchThreadID : SV_DispatchThreadID, // not used here
    uint3 groupThreadID : SV_GroupThreadID, // 2D coords in group
    uint groupIndex : SV_GroupIndex // linear index in [0..255]
)
{
    uint localIndex = groupIndex;

    RWStructuredBuffer<uint> histogram = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostProcessing::LuminanceHistogram)];
    uint countForThisBin = histogram[localIndex];
    histogramShared[localIndex] = countForThisBin * localIndex;

    // synchronize before zeroing
    GroupMemoryBarrierWithGroupSync();

    // Clear the bin for next frame
    histogram[localIndex] = 0;

    // Parallel reduce (prefix-sum style) in shared memory
    for (uint cutoff = GROUP_SIZE >> 1; cutoff > 0; cutoff >>= 1)
    {
        if (localIndex < cutoff)
        {
            histogramShared[localIndex] += histogramShared[localIndex + cutoff];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Only one thread needs to compute and store the final exposure
    if (localIndex == 0)
    {
        // weighted log-average, normalized to [0..1], then offset by –1
        float weightedLogAverage = (histogramShared[0] / max(NUM_PIXELS - countForThisBin, 1.0)) - 1.0;

        // map back to luminance domain
        float weightedAvgLum = exp2((weightedLogAverage / 254.0) * LOG_LUMINANCE_RANGE + MIN_LOG_LUMINANCE);

        RWStructuredBuffer<float> s_target = ResourceDescriptorHeap[ResourceDescriptorIndex(Builtin::PostProcessing::AdaptedLuminance)];

        // fetch last frame's adapted luminance
        float lumLastFrame = s_target[0];

        // lerp toward new value
        float adaptedLum = lumLastFrame + (weightedAvgLum - lumLastFrame) * TIME_COEFFICIENT;

        // store updated adapted luminance
        s_target[0] = adaptedLum;
        
        
    }
}
