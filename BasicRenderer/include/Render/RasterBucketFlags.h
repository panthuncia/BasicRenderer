#pragma once

#include <cstdint>
#include <vector>

// Per-material flags for rasterization
enum MaterialRasterFlags : uint32_t {
	MaterialRasterFlagsNone = 0,
	MaterialRasterFlagsAlphaTest = 1 << 0,
};

// operators
inline MaterialRasterFlags operator|=(MaterialRasterFlags& a, MaterialRasterFlags b) {
	a = static_cast<MaterialRasterFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	return a;
}