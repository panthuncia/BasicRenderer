#pragma once

#include <memory>
#include <string>

class Scene;

namespace USDLoader {
	std::shared_ptr<Scene> LoadModel(std::string file);
}