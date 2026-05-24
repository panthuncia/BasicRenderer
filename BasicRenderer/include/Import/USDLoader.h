#pragma once

#include <memory>
#include <string>

#include <pxr/usd/usd/stage.h>

class Scene;

namespace USDLoader {
	struct ImportSettings {
		bool enableDoubleSidedNameHeuristic = true;
	};

	struct InMemoryStageOptions {
		std::string sourceIdentifier;
		std::string sourceDirectory;
		std::string layerIdentifierHint = "in_memory.usda";
		bool isUsdPackage = false;
	};

	std::shared_ptr<Scene> LoadModel(std::string file, const ImportSettings& settings);
	std::shared_ptr<Scene> LoadModel(std::string file);
	std::shared_ptr<Scene> LoadModelFromStage(
		const pxr::UsdStageRefPtr& stage,
		const InMemoryStageOptions& options,
		const ImportSettings& settings = {});
	std::shared_ptr<Scene> LoadModelFromUsdBytes(
		const std::string& usdText,
		const InMemoryStageOptions& options,
		const ImportSettings& settings = {});
}