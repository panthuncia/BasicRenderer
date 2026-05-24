#pragma once

#include <memory>
#include <string>

#include "Import/USDLoader.h"

class Scene;

namespace NifLoader {

std::shared_ptr<Scene> LoadModel(std::string filePath, const USDLoader::ImportSettings& settings = {});

} // namespace NifLoader
