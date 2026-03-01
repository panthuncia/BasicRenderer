#include "Import/ModelLoader.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <spdlog/spdlog.h>

#include "Import/filetypes.h"
#include "Import/AssimpLoader.h"
#include "Import/GlTFLoader.h"
#include "Import/USDLoader.h"

std::shared_ptr<Scene> LoadModel(std::string filePath) {

	// Check if the file exists
	if (!std::filesystem::is_regular_file(filePath)) {
		spdlog::error("Model file not found: {}", filePath);
		return nullptr;
	}

	std::shared_ptr<Scene> scene;

	// Select loader based on file extension
	std::string extension = std::filesystem::path(filePath).extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	SceneLoader loader = GetSceneLoader(extension);
	
	switch(loader) {
		case SceneLoader::GlTF:
			scene = GlTFLoader::LoadModel(filePath);
			break;
		case SceneLoader::Assimp:
			scene = AssimpLoader::LoadModel(filePath);
			break;
		case SceneLoader::OpenUSD:
			scene = USDLoader::LoadModel(filePath);
			break;
	}

	return scene;
}