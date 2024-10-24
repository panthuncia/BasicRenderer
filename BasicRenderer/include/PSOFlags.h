#pragma once

enum PSOFlags {
	PSO_FLAGS_NONE = 0,
	PSO_DOUBLE_SIDED = 1 << 0,
    PSO_SHADOW = 1 << 1,
	PSO_IMAGE_BASED_LIGHTING = 1 << 2,
};