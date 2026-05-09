#pragma once

#include <cstdint>
#include <cstddef>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "Mesh/ClusterLODTypes.h"

namespace CLodCache {

inline constexpr uint32_t kSchemaVersion = 41;

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
	std::vector<std::vector<std::byte>> pageBlobs;
};

struct GroupPayloadLayoutMetadata {
	std::optional<ClusterLODGroupChunk> groupChunkMetadata;
	std::vector<uint32_t> pageBlobSizes;
	std::vector<uint64_t> pageBlobOffsets;

	bool IsValid() const {
		return groupChunkMetadata.has_value() && pageBlobSizes.size() == pageBlobOffsets.size();
	}

	void Clear() {
		groupChunkMetadata.reset();
		pageBlobSizes.clear();
		pageBlobOffsets.clear();
	}
};

std::wstring ResolveContainerPath(const ClusterLODCacheSource& cacheSource);

uint64_t ComputeBuildConfigHash();
std::wstring BuildCacheFileName(const CacheKey& key, uint64_t buildConfigHash);

std::optional<CacheData> TryLoad(const CacheKey& key, uint64_t expectedBuildConfigHash);
bool Save(const CacheKey& key, const CacheData& data);
bool Save(const CacheKey& key, uint64_t buildConfigHash, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload);
bool Save(const CacheKey& key, uint64_t buildConfigHash, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload, ClusterLODPrebuiltData* outSavedPrebuiltData);
bool LoadGroupPayload(const CacheData& cacheData, uint32_t groupLocalIndex, LoadedGroupPayload& outPayload);
bool LoadGroupPayload(const ClusterLODCacheSource& cacheSource, uint32_t groupLocalIndex, LoadedGroupPayload& outPayload);

// Load a single group payload from an already-opened container stream using a
// pre-resolved disk locator. Avoids re-opening the file and re-reading the
// directory for every request, giving a large throughput improvement when many
// groups are streamed from the same container.
bool LoadGroupPayloadDirect(std::ifstream& file,
	const ClusterLODGroupDiskLocator& locator,
	LoadedGroupPayload& outPayload);

// Selective variant: reads all metadata but only fetches page blobs where
// segmentNeedsFetch[i] is true.  Skipped segments get an empty blob in
// outPayload.pageBlobs.  segmentNeedsFetch.size() must equal the on-disk
// page count or be empty (which falls back to reading everything).
bool LoadGroupPayloadSelective(std::ifstream& file,
	const ClusterLODGroupDiskLocator& locator,
	const std::vector<bool>& segmentNeedsFetch,
	LoadedGroupPayload& outPayload);

bool GetGroupPayloadLayout(std::ifstream& file,
	const ClusterLODGroupDiskLocator& locator,
	GroupPayloadLayoutMetadata& outLayout);

bool LoadGroupPayloadSelectiveDirectStorage(std::ifstream& file,
	const std::wstring& containerPath,
	const ClusterLODGroupDiskLocator& locator,
	const std::vector<bool>& segmentNeedsFetch,
	LoadedGroupPayload& outPayload,
	std::string* outMessage = nullptr);

// Open a container file and validate its header.  Returns true on success.
// The caller keeps the ifstream around for repeated LoadGroupPayloadDirect
// calls, avoiding per-request open/close overhead.
bool OpenContainerFile(const ClusterLODCacheSource& cacheSource,
	std::ifstream& outFile,
	uint32_t& outGroupCount);

}
