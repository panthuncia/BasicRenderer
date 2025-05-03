#pragma once

#include <unordered_map> 
#include <string>
#include <memory>
#include <chrono>
#include <ctime>  
#include <functional>
#include <flecs.h>
#include <atomic>
#include "Animation/Skeleton.h"
#include "Managers/LightManager.h"
#include "Import/MeshData.h"
#include "Managers/ManagerInterface.h"

class DynamicGloballyIndexedResource;

class Scene {
public:
    Scene();
    ~Scene();
    flecs::entity CreateDirectionalLightECS(std::wstring name, DirectX::XMFLOAT3 color, float intensity, DirectX::XMFLOAT3 direction, bool shadowCasting = true);
    flecs::entity CreatePointLightECS(std::wstring name, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 color, float intensity, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0, bool shadowCasting = true);
    flecs::entity CreateSpotLightECS(std::wstring name, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 color, float intensity, DirectX::XMFLOAT3 direction, float innerConeAngle, float outerConeAngle, float constantAttenuation = 0, float linearAttenuation = 0, float quadraticAttenuation = 0, bool shadowCasting = true);
	flecs::entity CreateNodeECS(std::wstring name = L"");
	flecs::entity CreateRenderableEntityECS(const std::vector<std::shared_ptr<Mesh>>& meshes, std::wstring name);
    flecs::entity GetRoot() const;
    void Update();
    void SetCamera(DirectX::XMFLOAT3 lookAt, DirectX::XMFLOAT3 up, float fov, float aspect, float zNear, float zFar);
    flecs::entity& GetPrimaryCamera();
    void AddSkeleton(std::shared_ptr<Skeleton>);
    void PostUpdate();
    std::shared_ptr<Scene> AppendScene(std::shared_ptr<Scene> scene);
    //LightManager& GetLightManager();
    void Activate(ManagerInterface managerInterface);
	std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraOpaqueIndirectCommandBuffer();
    std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraAlphaTestIndirectCommandBuffer();
	std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraBlendIndirectCommandBuffer();
    std::shared_ptr<DynamicGloballyIndexedResource> GetPrimaryCameraMeshletFrustrumCullingIndirectCommandBuffer();
    void ProcessEntitySkins(bool overrideExistingSkins = false);
    std::shared_ptr<Scene> Clone() const;
	void DisableShadows();

private:
    static std::atomic<uint64_t> globalSceneCount;
	uint64_t m_sceneID = 0;
    std::vector<std::shared_ptr<Scene>> m_childScenes;
    flecs::entity m_primaryCamera;

	std::unordered_map<uint64_t, flecs::entity> animatedEntitiesByID;
	UINT numObjects = 0;
    std::vector<std::shared_ptr<Skeleton>> skeletons;
    std::vector<std::shared_ptr<Skeleton>> animatedSkeletons;
    std::chrono::system_clock::time_point lastUpdateTime = std::chrono::system_clock::now();

    // ECS
    flecs::entity ECSSceneRoot;

	ManagerInterface m_managerInterface;

    std::function<void(std::vector<float>)> setDirectionalLightCascadeSplits;
    std::function<uint8_t()> getNumDirectionalLightCascades;
    std::function<float()> getMaxShadowDistance;
	std::function<bool()> getMeshShadersEnabled;

    void MakeResident();
	void MakeNonResident();
    flecs::entity CreateLightECS(std::wstring name, Components::LightType type, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 color, float intensity, DirectX::XMFLOAT3 attenuation = { 0, 0, 0 }, DirectX::XMFLOAT3 direction = { 0, 0, 0 }, float innerConeAngle = 0, float outerConeAngle = 0, bool shadowCasting = false);
    void ActivateRenderable(flecs::entity& entity);
	void ActivateLight(flecs::entity& entity);
	void ActivateCamera(flecs::entity& entity);

	void ActivateAllAnimatedEntities();
};