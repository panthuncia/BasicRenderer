#pragma once

// Common output type for the geometry-extraction + CLod-cache-building phase
// of all asset loaders.  This struct contains everything needed to
// either (a) save a CLod cache on disk (headless CLI tool) or (b) proceed to
// GPU Mesh creation (renderer).

#include <optional>
#include <string>

#include "Import/CLodCacheLoader.h"
#include "Mesh/ClusterLODTypes.h"

struct MeshPreprocessResult {
	MeshIngestBuilder ingest;
	CLodCacheLoader::MeshCacheIdentity cacheIdentity;
	std::optional<ClusterLODPrebuiltData> prebuiltData;
	bool forceDoubleSidedPreview = false;

	MeshPreprocessResult(
		MeshIngestBuilder&& ingestData,
		CLodCacheLoader::MeshCacheIdentity&& identity,
		std::optional<ClusterLODPrebuiltData>&& prebuilt,
		bool forceDoubleSidedPreviewMaterial = false)
		: ingest(std::move(ingestData))
		, cacheIdentity(std::move(identity))
		, prebuiltData(std::move(prebuilt))
		, forceDoubleSidedPreview(forceDoubleSidedPreviewMaterial) {
	}
};
