#include "Import/NifLoader.h"

#include <filesystem>

#include <spdlog/spdlog.h>

#include "Import/BRNiflyClient.h"
#include "Scene/Scene.h"

namespace NifLoader {

std::shared_ptr<Scene> LoadModel(std::string filePath, const USDLoader::ImportSettings& settings)
{
    std::string errorMessage;
    auto package = BRNiflyClient::ConvertNifToUsd(filePath, {}, &errorMessage);
    if (!package) {
        spdlog::error("NIF import failed for '{}': {}", filePath, errorMessage);
        return nullptr;
    }

    for (const auto& diagnostic : package->diagnostics) {
        if (diagnostic.level == "warning") {
            spdlog::warn("BRNifly: {}", diagnostic.message);
        }
        else if (diagnostic.level == "error") {
            spdlog::error("BRNifly: {}", diagnostic.message);
        }
        else {
            spdlog::info("BRNifly: {}", diagnostic.message);
        }
    }

    USDLoader::InMemoryStageOptions options{};
    options.sourceIdentifier = package->sourceIdentifier;
    options.sourceDirectory = std::filesystem::path(filePath).parent_path().string();
    options.layerIdentifierHint = "brnifly_" + package->contentHash + ".usda";

    return USDLoader::LoadModelFromUsdBytes(package->rootLayerText, options, settings);
}

} // namespace NifLoader
