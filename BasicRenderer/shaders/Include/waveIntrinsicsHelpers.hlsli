#ifndef WAVE_INTRINSICS_HELPERS_HLSLI
#define WAVE_INTRINSICS_HELPERS_HLSLI

uint CountBits128(uint4 m)
{
    return countbits(m.x) + countbits(m.y) + countbits(m.z) + countbits(m.w);
}

uint GetWaveGroupLeaderLane(uint4 mask)
{
    // Find highest set bit across 128-bit mask (DX pattern)
    int4 highLanes = (int4) (firstbithigh(mask) | uint4(0, 0x20, 0x40, 0x60));
    uint highLane = (uint) max(max(max(highLanes.x, highLanes.y), highLanes.z), highLanes.w);
    return highLane; // lane index in [0, waveSize)
}

bool IsWaveGroupLeader(uint4 mask)
{
    // Find highest set bit across the 128-bit mask (matches MS docs pattern)
    int4 highLanes = (int4) (firstbithigh(mask) | uint4(0, 0x20, 0x40, 0x60));
    uint highLane = (uint) max(max(max(highLanes.x, highLanes.y), highLanes.z), highLanes.w);
    return WaveGetLaneIndex() == highLane;
}

// For a given mask + lane index, count how many lanes in the group come before us
uint GetLaneRankInGroup(uint4 mask, uint laneIndex)
{
    uint word = laneIndex >> 5; // / 32
    uint bit = laneIndex & 31; // % 32

    uint4 prefixMask = uint4(0, 0, 0, 0);

    // Lanes in earlier 32-bit words: keep whole mask
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        if (i < word)
        {
            prefixMask[i] = mask[i];
        }
        else if (i == word)
        {
            // Only keep bits below our bit in this word
            uint lowerBitsMask = (bit == 0) ? 0u : ((1u << bit) - 1u);
            prefixMask[i] = mask[i] & lowerBitsMask;
        }
        else
        {
            prefixMask[i] = 0;
        }
    }

    return CountBits128(prefixMask);
}

uint WaveFirstLaneFromMask(uint4 m)
{
    int b = firstbitlow(m.x);
    if (b >= 0)
        return (uint) b;

    b = firstbitlow(m.y);
    if (b >= 0)
        return 32u + (uint) b;

    b = firstbitlow(m.z);
    if (b >= 0)
        return 64u + (uint) b;

    b = firstbitlow(m.w);
    return 96u + (uint) b; // assuming some bit is set
}

bool WaveMaskIsFull(uint4 m)
{
    // active lanes mask
    uint4 active = WaveActiveBallot(true);
    return all(m == active);
}


#endif // WAVE_INTRINSICS_HELPERS_HLSLI