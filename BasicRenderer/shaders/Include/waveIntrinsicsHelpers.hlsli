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

#endif // WAVE_INTRINSICS_HELPERS_HLSLI