#pragma once

#include <optional>
#include <string>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>

#include "Mesh/Mesh.h"

namespace CLodCacheLoader {

struct MeshCacheIdentity {
	std::string sourceIdentifier;
	std::string primPath;
	std::string subsetName;
};

MeshCacheIdentity BuildIdentity(
	const pxr::UsdGeomMesh& mesh,
	const pxr::UsdStageRefPtr& stage,
	const std::string& subsetName);

std::optional<ClusterLODPrebuiltData> TryLoadPrebuilt(const MeshCacheIdentity& identity);
bool SavePrebuilt(const MeshCacheIdentity& identity, const ClusterLODPrebuiltData& prebuiltData);

}
