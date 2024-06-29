#pragma once

#include <unordered_map> 
#include <string>
#include <memory>
#include "RenderableObject.h"

class Scene {
public:
    UINT addObject(std::shared_ptr<RenderableObject> object);
    std::shared_ptr<RenderableObject> getObjectByName(const std::string& name);
    std::shared_ptr<RenderableObject> getObjectByID(UINT id);
    void removeObjectByName(const std::string& name);
    void removeObjectByID(UINT id);

private:
    std::unordered_map<std::string, std::shared_ptr<RenderableObject>> objectsByName;
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>> objectsByID;
	UINT numObjects = 0;
	UINT nextNodeID = 0;
};