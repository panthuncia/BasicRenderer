#pragma once

#include <unordered_map> 
#include <string>
#include <memory>
#include "RenderableObject.h"
#include <chrono>
#include <ctime>  
#include "Mesh.h"

class Scene {
public:
    UINT AddObject(std::shared_ptr<RenderableObject> object);
    UINT AddNode(std::shared_ptr<SceneNode> node);
    std::shared_ptr<SceneNode> CreateNode(std::string name = ""); // Like addNode, if node ids need to be pre-assigned
    std::shared_ptr<RenderableObject> CreateRenderableObject(MeshData meshData, std::string name); // Like addObject, if node ids need to be pre-assigned
    std::shared_ptr<RenderableObject> GetObjectByName(const std::string& name);
    std::shared_ptr<RenderableObject> GetObjectByID(UINT id);
    void RemoveObjectByName(const std::string& name);
    void RemoveObjectByID(UINT id);
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetRenderableObjectIDMap();
    SceneNode& GetRoot();
    void Update();

private:
    std::unordered_map<std::string, std::shared_ptr<RenderableObject>> objectsByName;
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>> objectsByID;
    std::unordered_map<UINT, std::shared_ptr<SceneNode>> nodesByID;
    std::unordered_map<std::string, std::shared_ptr<SceneNode>> nodesByName;
	UINT numObjects = 0;
	UINT nextNodeID = 0;
    SceneNode sceneRoot;
    std::chrono::system_clock::time_point lastUpdateTime = std::chrono::system_clock::now();
};