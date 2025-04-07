#pragma once

#include <unordered_map> 
#include <string>
#include <memory>
#include "RenderableObject.h"
#include <chrono>
#include <ctime>  
#include <functional>
#include <flecs.h>
#include "Camera.h"
#include "Skeleton.h"
#include "LightManager.h"
#include "MeshData.h"
#include "ManagerInterface.h"

class DynamicGloballyIndexedResource;
class Light;

class Scene {
public:
    Scene();
    ~Scene();
    UINT AddObject(std::shared_ptr<RenderableObject> object);
    UINT AddNode(std::shared_ptr<SceneNode> node, bool canAttachToRoot = true);
    UINT AddLight(std::shared_ptr<Light> light);
    flecs::entity CreateDirectionalLightECS(std::wstring name, XMFLOAT3 color, float intensity, XMFLOAT3 direction);
    flecs::entity CreatePointLightECS(std::wstring name, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0);
    flecs::entity CreateSpotLightECS(std::wstring name, XMFLOAT3 position, XMFLOAT3 color, float intensity, XMFLOAT3 direction, float innerConeAngle, float outerConeAngle, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0);
    std::shared_ptr<SceneNode> CreateNode(std::wstring name = L""); // Like addNode, if node ids need to be pre-assigned
	flecs::entity CreateNodeECS(std::wstring name = L"");
    std::shared_ptr<RenderableObject> CreateRenderableObject(const std::vector<std::shared_ptr<Mesh>>& meshes, std::wstring name); // Like addObject, if node ids need to be pre-assigned
	flecs::entity CreateRenderableEntityECS(const std::vector<std::shared_ptr<Mesh>>& meshes, std::wstring name);
    std::shared_ptr<RenderableObject> GetObjectByName(const std::wstring& name);
    std::shared_ptr<RenderableObject> GetObjectByID(UINT id);
    std::shared_ptr<SceneNode> GetEntityByID(UINT id);
    void RemoveObjectByName(const std::wstring& name);
    void RemoveObjectByID(UINT id);
    void RemoveLightByID(UINT Id);
	void RemoveNodeByID(UINT id);
	void RemoveEntityByID(UINT id, bool recurse = false);
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetRenderableObjectIDMap();
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetOpaqueRenderableObjectIDMap();
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetAlphaTestRenderableObjectIDMap();
	std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetBlendRenderableObjectIDMap();
	std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetOpaqueSkinnedRenderableObjectIDMap();
	std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetAlphaTestSkinnedRenderableObjectIDMap();
	std::unordered_map<UINT, std::shared_ptr<RenderableObject>>& GetBlendSkinnedRenderableObjectIDMap();
    std::unordered_map<UINT, std::shared_ptr<Light>>& GetLightIDMap();
    flecs::entity GetRoot();
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
    void Activate(ManagerInterface managerInterface);
	std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraOpaqueIndirectCommandBuffer();
    std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraAlphaTestIndirectCommandBuffer();
	std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraBlendIndirectCommandBuffer();

private:

    std::shared_ptr<Camera> pCamera;

    std::unordered_map<std::wstring, std::shared_ptr<RenderableObject>> objectsByName;
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>> objectsByID;
    std::unordered_map<UINT, std::shared_ptr<SceneNode>> nodesByID;
    std::unordered_map<std::wstring, std::shared_ptr<SceneNode>> nodesByName;
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>> opaqueObjectsByID;
    std::unordered_map<UINT, std::shared_ptr<RenderableObject>> alphaTestObjectsByID;
	std::unordered_map<UINT, std::shared_ptr<RenderableObject>> blendObjectsByID;
	std::unordered_map<UINT, std::shared_ptr<RenderableObject>> opaqueSkinnedObjectsByID;
	std::unordered_map<UINT, std::shared_ptr<RenderableObject>> alphaTestSkinnedObjectsByID;
	std::unordered_map<UINT, std::shared_ptr<RenderableObject>> blendSkinnedObjectsByID;
    std::unordered_map<UINT, std::shared_ptr<Light>> lightsByID;
	std::unordered_map<UINT, std::shared_ptr<Mesh>> meshesByID;
	std::unordered_map<uint64_t, flecs::entity> animatedEntitiesByID;
	UINT numObjects = 0;
	UINT nextNodeID = 0;
    std::vector<std::shared_ptr<Skeleton>> skeletons;
    std::vector<std::shared_ptr<Skeleton>> animatedSkeletons;
    std::chrono::system_clock::time_point lastUpdateTime = std::chrono::system_clock::now();

    std::shared_ptr<DynamicGloballyIndexedResource> m_pPrimaryCameraOpaqueIndirectCommandBuffer;
	std::shared_ptr<DynamicGloballyIndexedResource> m_pPrimaryCameraAlphaTestIndirectCommandBuffer;
	std::shared_ptr<DynamicGloballyIndexedResource> m_pPrimaryCameraBlendIndirectCommandBuffer;

    // ECS
    flecs::entity ECSSceneRoot;

	ManagerInterface m_managerInterface;

    std::function<void(std::vector<float>)> setDirectionalLightCascadeSplits;
    std::function<uint8_t()> getNumDirectionalLightCascades;
    std::function<float()> getMaxShadowDistance;

    void MakeResident();
	void MakeNonResident();
    flecs::entity CreateLightECS(std::wstring name, Components::LightType type, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0, XMFLOAT3 direction = { 0, 0, 0 }, float innerConeAngle = 0, float outerConeAngle = 0);

};