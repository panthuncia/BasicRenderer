#pragma once

#include <cstdint>
#include <cstddef>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "Mesh/Mesh.h"

namespace CLodCache {

inline constexpr uint32_t kSchemaVersion = 16;

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
bool Save(const CacheKey& key, uint64_t buildConfigHash, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload);
bool LoadGroupPayload(const CacheData& cacheData, uint32_t groupLocalIndex, LoadedGroupPayload& outPayload);
bool LoadGroupPayload(const ClusterLODCacheSource& cacheSource, uint32_t groupLocalIndex, LoadedGroupPayload& outPayload);

/// Load a single group payload from an already-opened container stream using a
/// pre-resolved disk locator. Avoids re-opening the file and re-reading the
/// directory for every request, giving a large throughput improvement when many
/// groups are streamed from the same container.
bool LoadGroupPayloadDirect(std::ifstream& file,
	const ClusterLODGroupDiskLocator& locator,
	LoadedGroupPayload& outPayload);

/// Open a container file and validate its header.  Returns true on success.
/// The caller keeps the ifstream around for repeated LoadGroupPayloadDirect
/// calls, avoiding per-request open/close overhead.
bool OpenContainerFile(const ClusterLODCacheSource& cacheSource,
	std::ifstream& outFile,
	uint32_t& outGroupCount);

}
