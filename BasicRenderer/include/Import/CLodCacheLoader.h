#pragma once

#include <optional>
#include <string>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usdGeom/mesh.h>

#include "Mesh/ClusterLODTypes.h"

namespace CLodCacheLoader {

struct MeshCacheIdentity {
	std::string sourceIdentifier;
	std::string primPath;
	std::string subsetName;
};

MeshCacheIdentity BuildIdentity(
	const pxr::UsdGeomMesh& mesh,
	const pxr::UsdStageRefPtr& stage,
	const std::string& subsetName,
	pxr::UsdTimeCode geomTimeCode = pxr::UsdTimeCode::Default());

std::optional<ClusterLODPrebuiltData> TryLoadPrebuilt(const MeshCacheIdentity& identity);
bool SavePrebuilt(const MeshCacheIdentity& identity, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload);
bool SavePrebuiltLocked(const MeshCacheIdentity& identity, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload);
bool SavePrebuilt(const MeshCacheIdentity& identity, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload, ClusterLODPrebuiltData* outSavedPrebuiltData);
bool SavePrebuiltLocked(const MeshCacheIdentity& identity, const ClusterLODPrebuiltData& prebuiltData, const ClusterLODCacheBuildPayload& payload, ClusterLODPrebuiltData* outSavedPrebuiltData);

}
