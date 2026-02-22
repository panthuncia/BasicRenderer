#pragma once
#include <unordered_set>
#include <cstdint>

#include "Render/RenderPhase.h"
#include "Render/RasterBucketFlags.h"

enum MaterialCompileFlags : uint64_t {
	MaterialCompileNone = 0,
	MaterialCompileBlend = 1 << 0,
	MaterialCompileAlphaTest = 1 << 1,
	MaterialCompileDoubleSided = 1 << 2,
	MaterialCompileBaseColorTexture = 1 << 3,
	MaterialCompileNormalMap = 1 << 4,
	MaterialCompilePBRMaps = 1 << 5,
	MaterialCompileAOTexture = 1 << 6,
	MaterialCompileEmissiveTexture = 1 << 7,
	MaterialCompileParallax = 1 << 8,
};

// |= operator for MaterialCompileFlags
inline MaterialCompileFlags operator|=(MaterialCompileFlags& a, MaterialCompileFlags b) {
	a = static_cast<MaterialCompileFlags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
	return a;
}

struct TechniqueDescriptor {
	std::unordered_set<RenderPhase> passes; // Which render passes (that do per-object work) this technique participates in
	MaterialCompileFlags compileFlags = static_cast<MaterialCompileFlags>(0); // Any difference here requires a separate material eval PSO
	// metadata: alphaMode, domain (Deferred/Forward), feature bits (aniso, parallax)?
	// TechniqueDescriptor::Hasher- just use the compileFlags as the hash, the passes are calculated from that
	MaterialRasterFlags rasterFlags = MaterialRasterFlagsNone; // Any difference here requires a separate raster PSO
	struct Hasher {
		size_t operator()(TechniqueDescriptor const& td) const noexcept {
			return std::hash<uint64_t>()(static_cast<uint64_t>(td.compileFlags));
		}
	};
	// Equality operator
	bool operator==(TechniqueDescriptor const& o) const noexcept {
		return (compileFlags == o.compileFlags);
	}

};
