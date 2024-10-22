#pragma once

enum PSOFlags {
    PSO_VERTEX_COLORS = 1 << 0,
    PSO_BASE_COLOR_TEXTURE = 1 << 1,
    PSO_NORMAL_MAP = 1 << 2,
    PSO_AO_TEXTURE = 1 << 3,
    PSO_EMISSIVE_TEXTURE = 1 << 4,
    PSO_PBR = 1 << 5,
    PSO_PBR_MAPS = 1 << 6,
    PSO_SKINNED = 1 << 7,
    PSO_DOUBLE_SIDED = 1 << 8,
    PSO_PARALLAX = 1 << 9,
    PSO_SHADOW = 1 << 10,
	PSO_IMAGE_BASED_LIGHTING = 1 << 11,
};