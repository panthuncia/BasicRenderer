#pragma once

#include <unordered_map> 
#include <string>
#include <memory>
#include <chrono>
#include <ctime>  
#include <functional>
#include <flecs.h>
#include "Skeleton.h"
#include "LightManager.h"
#include "MeshData.h"
#include "ManagerInterface.h"

class DynamicGloballyIndexedResource;

class Scene {
public:
    Scene();
    ~Scene();
    flecs::entity CreateDirectionalLightECS(std::wstring name, XMFLOAT3 color, float intensity, XMFLOAT3 direction);
    flecs::entity CreatePointLightECS(std::wstring name, XMFLOAT3 position, XMFLOAT3 color, float intensity, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0);
    flecs::entity CreateSpotLightECS(std::wstring name, XMFLOAT3 position, XMFLOAT3 color, float intensity, XMFLOAT3 direction, float innerConeAngle, float outerConeAngle, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0);
	flecs::entity CreateNodeECS(std::wstring name = L"");
	flecs::entity CreateRenderableEntityECS(const std::vector<std::shared_ptr<Mesh>>& meshes, std::wstring name);
    flecs::entity GetRoot();
    void Update();
    void SetCamera(XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar);
    flecs::entity& GetPrimaryCamera();
    void AddSkeleton(std::shared_ptr<Skeleton>);
    void PostUpdate();
    std::shared_ptr<SceneNode> AppendScene(Scene& scene);
    //LightManager& GetLightManager();
    void Activate(ManagerInterface managerInterface);
	std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraOpaqueIndirectCommandBuffer();
    std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraAlphaTestIndirectCommandBuffer();
	std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraBlendIndirectCommandBuffer();

private:

    flecs::entity m_primaryCamera;

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