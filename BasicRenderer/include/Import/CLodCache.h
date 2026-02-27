#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Mesh/Mesh.h"

namespace CLodCache {

inline constexpr uint32_t kSchemaVersion = 6;

struct CacheKey {
	std::string sourceIdentifier;
	std::string primPath;
	std::string subsetName;
};

struct CacheData {
	uint32_t schemaVersion = kSchemaVersion;
	uint64_t buildConfigHash = 0;
	ClusterLODPrebuiltData prebuiltData;
};

uint64_t ComputeBuildConfigHash();
std::wstring BuildCacheFileName(const CacheKey& key, uint64_t buildConfigHash);

std::optional<CacheData> TryLoad(const CacheKey& key, uint64_t expectedBuildConfigHash);
bool Save(const CacheKey& key, const CacheData& data);

}
