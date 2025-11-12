#pragma once
#include <unordered_set>
#include <cstdint>

#include "Render/RenderPhase.h"

enum MaterialCompileFlags : uint64_t {
	MaterialCompileBlend = 1 << 0,
	MaterialCompileAlphaTest = 1 << 1,
	MaterialCompileDoubleSided = 1 << 2,
};

// |= operator for MaterialCompileFlags
inline MaterialCompileFlags operator|=(MaterialCompileFlags& a, MaterialCompileFlags b) {
	a = static_cast<MaterialCompileFlags>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
	return a;
}

struct TechniqueDescriptor {
	std::unordered_set<RenderPhase> passes; // Which render passes (that do per-object work) this technique participates in
	MaterialCompileFlags compileFlags = static_cast<MaterialCompileFlags>(0); // Any difference here requires a separate PSO
	// metadata: alphaMode, domain (Deferred/Forward), feature bits (aniso, parallax)?
};