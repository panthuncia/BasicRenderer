#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Mesh/Mesh.h"

namespace CLodCache {

inline constexpr uint32_t kSchemaVersion = 11;

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

struct LoadedGroupPayload {
	std::optional<ClusterLODGroupChunk> groupChunkMetadata;
	std::vector<std::byte> vertexChunk;
	std::vector<std::byte> skinningChunk;
	std::vector<uint32_t> meshletVertexChunk;
	std::vector<uint32_t> compressedPositionWordChunk;
	std::vector<uint32_t> compressedNormalWordChunk;
	std::vector<uint32_t> compressedMeshletVertexWordChunk;
	std::vector<meshopt_Meshlet> meshletChunk;
	std::vector<uint8_t> meshletTriangleChunk;
	std::vector<BoundingSphere> meshletBoundsChunk;
};

uint64_t ComputeBuildConfigHash();
std::wstring BuildCacheFileName(const CacheKey& key, uint64_t buildConfigHash);

std::optional<CacheData> TryLoad(const CacheKey& key, uint64_t expectedBuildConfigHash);
bool Save(const CacheKey& key, const CacheData& data);
bool Save(const CacheKey& key, uint32_t schemaVersion, uint64_t buildConfigHash, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload);
bool LoadGroupPayload(const CacheData& cacheData, uint32_t groupLocalIndex, LoadedGroupPayload& outPayload);
bool LoadGroupPayload(const ClusterLODCacheSource& cacheSource, uint32_t groupLocalIndex, LoadedGroupPayload& outPayload);

}
