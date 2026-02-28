#include "Import/CLodCacheLoader.h"

#include "Import/CLodCache.h"

namespace CLodCacheLoader {

namespace {
	constexpr const char* kFullMeshSubsetSentinel = "__clod_full_mesh__";

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
	auto cached = CLodCache::TryLoad(cacheKey, buildHash);
	if (!cached.has_value()) {
		return std::nullopt;
	}

	return std::move(cached->prebuiltData);
}

bool SavePrebuilt(const MeshCacheIdentity& identity, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload)
{
	const auto cacheKey = ToCacheKey(identity);
	const uint64_t buildHash = CLodCache::ComputeBuildConfigHash();
	return CLodCache::Save(cacheKey, CLodCache::kSchemaVersion, buildHash, prebuiltData, payload);
}

}
