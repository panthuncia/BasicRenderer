#pragma once

#include <vector>

enum class MaterialBuckets {
	Opaque,
	AlphaTest,
	Blend
};

inline std::vector<MaterialBuckets> MaterialBucketTypes = {MaterialBuckets::Opaque, MaterialBuckets::AlphaTest, MaterialBuckets::Blend };