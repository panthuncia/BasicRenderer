#pragma once

#include <vector>
#include <string>

#include "SceneNode.h"
#include "Mesh.h"

class RenderableObject : public SceneNode {
public:
	RenderableObject(std::string name);
	RenderableObject(std::string name, std::vector<Mesh> meshes);
	std::vector<Mesh>& getMeshes();
private:
	std::vector<Mesh> meshes;
};