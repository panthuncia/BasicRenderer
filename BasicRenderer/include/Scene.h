#pragma once

#include <unordered_map> 
#include <string>
#include <memory>
#include "RenderableObject.h"
#include <chrono>
#include <ctime>  
#include "Mesh.h"
#include "Camera.h"
#include "Skeleton.h"
#include "LightManager.h"

class Scene {
public:
    UINT AddObject(std::shared_ptr<RenderableObject> object);
    UINT AddNode(std::shared_ptr<SceneNode> node);
    UINT AddLight(std::shared_ptr<Light> light);
    std::shared_ptr<SceneNode> CreateNode(std::string name = ""); // Like addNode, if node ids need to be pre-assigned
    std::shared_ptr<RenderableObject> CreateRenderableObject(MeshData meshData, std::string name); // Like addObject, if node ids need to be pre-assigned
    std::shared_ptr<RenderableObject> GetObjectByName(const std::string& name);
    std::shared_ptr<RenderableObject> GetObjectByID(UINT id);
    void RemoveObjectByName(const std::string& name);
    void RemoveObjectByID(UINT id);
    void RemoveLightByID(UINT Id);
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetRenderableObjectIDMap();
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetOpaqueRenderableObjectIDMap();
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetTransparentRenderableObjectIDMap();
    SceneNode& GetRoot();
    void Update();
    void SetCamera(XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar);
    std::shared_ptr<Camera> GetCamera();
    void AddSkeleton(std::shared_ptr<Skeleton>);
    UINT GetNumLights();
    UINT GetLightBufferDescriptorIndex();
    void PostUpdate();
    //LightManager& GetLightManager();

private:
    std::shared_ptr<Camera> pCamera;

    std::unordered_map<std::string, std::shared_ptr<RenderableObject>> objectsByName;
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>> objectsByID;
    std::unordered_map<UINT, std::shared_ptr<SceneNode>> nodesByID;
    std::unordered_map<std::string, std::shared_ptr<SceneNode>> nodesByName;
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>> opaqueObjectsByID;
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>> transparentObjectsByID;
    std::unordered_map<UINT, std::shared_ptr<Light>> lightsByID;
	UINT numObjects = 0;
	UINT nextNodeID = 0;
    SceneNode sceneRoot;
    std::vector<std::shared_ptr<Skeleton>> skeletons;
    std::vector<std::shared_ptr<Skeleton>> animatedSkeletons;
    std::chrono::system_clock::time_point lastUpdateTime = std::chrono::system_clock::now();
    LightManager lightManager;
};