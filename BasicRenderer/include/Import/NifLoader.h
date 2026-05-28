#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <filesystem>
#include <string>

#include "Import/USDLoader.h"

class Scene;

namespace NifLoader {

struct LoadTimingStats {
	bool cacheHit = false;
	bool payloadCacheHit = false;
	bool assetCacheWritten = false;
	std::uint64_t cacheProbeMs = 0;
	std::uint64_t brniflyMs = 0;
	std::uint64_t assetWriteMs = 0;
	std::uint64_t usdLoadMs = 0;
	std::uint64_t usdOpenMs = 0;
	std::uint64_t usdExtractMs = 0;
	std::uint64_t meshBuildMs = 0;
	std::filesystem::path cachePath;
	std::string sourceIdentifier;
	std::string contentHash;
};

struct CachedAssetLoadResult {
	std::shared_ptr<Scene> scene;
	std::filesystem::path cachePath;
	std::string sourceIdentifier;
	std::string contentHash;
};

std::optional<CachedAssetLoadResult> TryLoadCachedModel(std::string cacheKey, const USDLoader::ImportSettings& settings = {}, LoadTimingStats* stats = nullptr);
std::optional<USDLoader::ImportedAssetPayload> TryLoadCachedImportedAsset(std::string cacheKey, const USDLoader::ImportSettings& settings = {}, LoadTimingStats* stats = nullptr);
std::optional<USDLoader::ImportedAssetPayload> LoadImportedAssetWithCacheKey(std::string filePath, std::string cacheKey, const USDLoader::ImportSettings& settings = {}, LoadTimingStats* stats = nullptr);
std::shared_ptr<Scene> LoadModelWithCacheKey(std::string filePath, std::string cacheKey, const USDLoader::ImportSettings& settings = {}, LoadTimingStats* stats = nullptr);
std::shared_ptr<Scene> LoadModel(std::string filePath, const USDLoader::ImportSettings& settings = {});

} // namespace NifLoader
