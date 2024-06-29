#pragma once

#include <vector>
#include <string>

#include "SceneNode.h"
#include "Mesh.h"

class RenderableObject : public SceneNode {
	RenderableObject(std::string name);
	RenderableObject(std::string name, std::vector<Mesh> meshes);
private:
	std::vector<Mesh> meshes;
};