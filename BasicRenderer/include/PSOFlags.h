#pragma once

enum PSOFlags {
    VERTEX_COLORS = 1 << 0,
    BASE_COLOR_TEXTURE = 1 << 1,
    NORMAL_MAP = 1 << 2,
    AO_TEXTURE = 1 << 3,
    EMISSIVE_TEXTURE = 1 << 4,
    PBR = 1 << 5,
    PBR_MAPS = 1 << 6,
    SKINNED = 1 << 7,
    DOUBLE_SIDED = 1 << 8,
    PARALLAX = 1 << 9,
};