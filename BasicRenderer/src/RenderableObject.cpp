#include "RenderableObject.h"

RenderableObject::RenderableObject(std::string name) : SceneNode(name) {
}

RenderableObject::RenderableObject(std::string name, std::vector<Mesh> meshes) : SceneNode(name) {
	this->meshes = meshes;
}