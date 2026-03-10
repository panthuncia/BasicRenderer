#include "Import/CLodCacheLoader.h"

#include "Import/CLodCache.h"

#include <memory>
#include <mutex>
#include <unordered_map>

#include <spdlog/spdlog.h>

namespace CLodCacheLoader {

namespace {
	constexpr const char* kFullMeshSubsetSentinel = "__clod_full_mesh__";

		std::mutex& GetCacheSaveMutexForIdentity(const MeshCacheIdentity& identity)
		{
			static std::mutex cacheMutexTableGuard;
			static std::unordered_map<std::string, std::unique_ptr<std::mutex>> cacheMutexTable;

			const std::string key = identity.sourceIdentifier + "|" + identity.primPath + "|" + identity.subsetName;

			std::lock_guard<std::mutex> lock(cacheMutexTableGuard);
			auto& mutexPtr = cacheMutexTable[key];
			if (!mutexPtr) {
				mutexPtr = std::make_unique<std::mutex>();
			}
			return *mutexPtr;
		}

	std::string NormalizeSubsetName(const std::string& subsetName)
	{
		return subsetName.empty() ? std::string(kFullMeshSubsetSentinel) : subsetName;
	}

	CLodCache::CacheKey ToCacheKey(const MeshCacheIdentity& identity)
	{
		CLodCache::CacheKey key{};
		key.sourceIdentifier = identity.sourceIdentifier;
		key.primPath = identity.primPath;
		key.subsetName = NormalizeSubsetName(identity.subsetName);
		return key;
	}
}

MeshCacheIdentity BuildIdentity(
	const pxr::UsdGeomMesh& mesh,
	const pxr::UsdStageRefPtr& stage,
	const std::string& subsetName)
{
	MeshCacheIdentity identity{};
	if (stage && stage->GetRootLayer()) {
		identity.sourceIdentifier = stage->GetRootLayer()->GetIdentifier();
	}
	identity.primPath = mesh.GetPrim().GetPath().GetString();
	identity.subsetName = subsetName;
	return identity;
}

std::optional<ClusterLODPrebuiltData> TryLoadPrebuilt(const MeshCacheIdentity& identity)
{
	const auto cacheKey = ToCacheKey(identity);
	const uint64_t buildHash = CLodCache::ComputeBuildConfigHash();
	spdlog::debug("CLodCacheLoader::TryLoadPrebuilt  src='{}' prim='{}' subset='{}' hash=0x{:X}",
		identity.sourceIdentifier, identity.primPath, identity.subsetName, buildHash);
	auto cached = CLodCache::TryLoad(cacheKey, buildHash);
	if (!cached.has_value()) {
		spdlog::debug("  -> cache not found on disk.");
		return std::nullopt;
	}

	spdlog::debug("  -> cache HIT (groups={}).", cached->prebuiltData.groups.size());
	return std::move(cached->prebuiltData);
}

bool SavePrebuilt(const MeshCacheIdentity& identity, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload)
{
	const auto cacheKey = ToCacheKey(identity);
	const uint64_t buildHash = CLodCache::ComputeBuildConfigHash();
	return CLodCache::Save(cacheKey, buildHash, prebuiltData, payload);
}

bool SavePrebuiltLocked(const MeshCacheIdentity& identity, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload)
{
	spdlog::debug("CLodCacheLoader::SavePrebuiltLocked  src='{}' prim='{}' subset='{}'",
		identity.sourceIdentifier, identity.primPath, identity.subsetName);
	std::lock_guard<std::mutex> saveLock(GetCacheSaveMutexForIdentity(identity));
	if (TryLoadPrebuilt(identity).has_value()) {
		spdlog::debug("  -> already cached (race-condition guard).");
		return true;
	}

	bool ok = SavePrebuilt(identity, prebuiltData, payload);
	if (ok)
		spdlog::debug("  -> SavePrebuilt succeeded.");
	else
		spdlog::warn("  -> SavePrebuilt FAILED.");
	return ok;
}

}
