#pragma once

#include <vector>

enum class MaterialBuckets {
	Opaque,
	Transparent,
	Hair
};

inline std::vector<MaterialBuckets> MaterialBucketTypes = {MaterialBuckets::Opaque, MaterialBuckets::Transparent};