#pragma once

#include <unordered_map> 
#include <string>
#include <memory>
#include "RenderableObject.h"
#include <chrono>
#include <ctime>  
#include <functional>
#include "Camera.h"
#include "Skeleton.h"
#include "LightManager.h"
#include "MeshData.h"
#include "Light.h"
#include "MeshManager.h"
#include "ObjectManager.h"
#include "IndirectCommandBufferManager.h"

class DynamicGloballyIndexedResource;

class Scene {
public:
    Scene();
    UINT AddObject(std::shared_ptr<RenderableObject> object);
    UINT AddNode(std::shared_ptr<SceneNode> node);
    UINT AddLight(std::shared_ptr<Light> light, bool shadowCasting = false);
    std::shared_ptr<SceneNode> CreateNode(std::wstring name = L""); // Like addNode, if node ids need to be pre-assigned
    std::shared_ptr<RenderableObject> CreateRenderableObject(MeshData meshData, std::wstring name); // Like addObject, if node ids need to be pre-assigned
    std::shared_ptr<RenderableObject> GetObjectByName(const std::wstring& name);
    std::shared_ptr<RenderableObject> GetObjectByID(UINT id);
    std::shared_ptr<SceneNode> GetEntityByID(UINT id);
    void RemoveObjectByName(const std::wstring& name);
    void RemoveObjectByID(UINT id);
    void RemoveLightByID(UINT Id);
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetRenderableObjectIDMap();
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetOpaqueRenderableObjectIDMap();
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetTransparentRenderableObjectIDMap();
    std::unordered_map<UINT, std::shared_ptr<Light>>& GetLightIDMap();
    SceneNode& GetRoot();
    void Update();
    void SetCamera(XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar);
    std::shared_ptr<Camera> GetCamera();
    void AddSkeleton(std::shared_ptr<Skeleton>);
    UINT GetNumLights();
    UINT GetLightBufferDescriptorIndex();
    UINT GetPointCubemapMatricesDescriptorIndex();
    UINT GetSpotMatricesDescriptorIndex();
    UINT GetDirectionalCascadeMatricesDescriptorIndex();
    void PostUpdate();
    std::shared_ptr<SceneNode> AppendScene(Scene& scene);
    //LightManager& GetLightManager();
    void Activate();
    const std::unique_ptr<MeshManager>& GetMeshManager();
	const std::unique_ptr<ObjectManager>& GetObjectManager();
	std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraOpaqueIndirectCommandBuffer();
    std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraTransparentIndirectCommandBuffer();
	unsigned int GetNumDrawsInScene();
	unsigned int GetNumOpaqueDraws();
	unsigned int GetNumTransparentDraws();
	const std::unique_ptr<IndirectCommandBufferManager>& GetIndirectCommandBufferManager();

private:
    std::shared_ptr<Camera> pCamera;

    std::unordered_map<std::wstring, std::shared_ptr<RenderableObject>> objectsByName;
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>> objectsByID;
    std::unordered_map<UINT, std::shared_ptr<SceneNode>> nodesByID;
    std::unordered_map<std::wstring, std::shared_ptr<SceneNode>> nodesByName;
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>> opaqueObjectsByID;
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>> transparentObjectsByID;
    std::unordered_map<UINT, std::shared_ptr<Light>> lightsByID;
	std::unordered_map<UINT, std::shared_ptr<Mesh>> meshesByID;
	UINT numObjects = 0;
	UINT nextNodeID = 0;
    SceneNode sceneRoot;
    std::vector<std::shared_ptr<Skeleton>> skeletons;
    std::vector<std::shared_ptr<Skeleton>> animatedSkeletons;
    std::chrono::system_clock::time_point lastUpdateTime = std::chrono::system_clock::now();
    LightManager lightManager;
    std::unique_ptr<MeshManager> meshManager = nullptr;
	std::unique_ptr<ObjectManager> objectManager = nullptr;
	std::unique_ptr<IndirectCommandBufferManager> indirectCommandBufferManager = nullptr;
    std::shared_ptr<DynamicGloballyIndexedResource> m_pPrimaryCameraOpaqueIndirectCommandBuffer;
	std::shared_ptr<DynamicGloballyIndexedResource> m_pPrimaryCameraTransparentIndirectCommandBuffer;

	unsigned int m_numDrawsInScene = 0;
	unsigned int m_numOpaqueDraws = 0;
	unsigned int m_numTransparentDraws = 0;

    std::function<void(std::vector<float>)> setDirectionalLightCascadeSplits;
    std::function<uint8_t()> getNumDirectionalLightCascades;
    std::function<float()> getMaxShadowDistance;

    void MakeResident();
	void MakeNonResident();
};