#pragma once

#include <memory>

#include "Scene.h"
#include "Mesh.h"

std::shared_ptr<Scene> loadGLB(std::string file);