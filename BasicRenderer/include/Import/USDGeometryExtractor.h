#pragma once

#include <optional>
#include <string>
#include <vector>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdSkel/skinningQuery.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/tf/token.h>

#include "Import/MeshPreprocessData.h"

namespace USDGeometryExtractor {

// Extract geometry for a single mesh (or mesh+subset), populate
// MeshIngestBuilder, and build/load CLod cache.
MeshPreprocessResult ExtractSubMesh(
	const pxr::UsdGeomMesh& mesh,
	const std::optional<pxr::UsdGeomSubset>& subset,
	const pxr::UsdStageRefPtr& stage,
	double metersPerUnit,
	const std::string& uvSetName,
	const std::optional<pxr::UsdSkelSkinningQuery>& skinQ,
	const pxr::VtTokenArray& skelJointOrderRaw,
	const pxr::VtTokenArray& skelJointOrderMapped);

// Build a UsdSkelSkinningQuery for a mesh if it has skinning data.
std::optional<pxr::UsdSkelSkinningQuery> GetSkinningQuery(
	const pxr::UsdGeomMesh& mesh,
	const pxr::UsdSkelCache& skelCache);

// Results from ExtractAll (CLI stage-wide extraction).
struct StageExtractionResult {
	size_t meshesProcessed = 0;
	size_t submeshesProcessed = 0;
	size_t cachesBuilt = 0;
};

// Open a USD stage and extract geometry + build CLod caches for every mesh.
StageExtractionResult ExtractAll(const std::string& filePath);

} // namespace USDGeometryExtractor
