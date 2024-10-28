#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>
#include "buffers.h"
#include "ResourceHandles.h"
#include "Interfaces/ISceneNodeObserver.h"
#include "DynamicResource.h"

class Camera;
class Light;
class ShadowMaps;
class SceneNode;

class LightManager : public ISceneNodeObserver<Light>, public ISceneNodeObserver<SceneNode> {
public:
    LightManager();
    void Initialize();
    void AddLight(Light* lightNode, bool shadowCasting = false, Camera* currentCamera = nullptr);
    void RemoveLight(Light* light);
    unsigned int GetLightBufferDescriptorIndex();
    unsigned int GetPointCubemapMatricesDescriptorIndex();
    unsigned int GetSpotMatricesDescriptorIndex();
    unsigned int GetDirectionalCascadeMatricesDescriptorIndex();
    unsigned int GetNumLights();
    void SetCurrentCamera(Camera* camera);
    void OnNodeUpdated(SceneNode* camera) override;
    void OnNodeUpdated(Light* light) override;
    void UpdateBuffers();
    void UpdateNumObjectsInScene(unsigned int numObjects);

private:
    DynamicStructuredBufferHandle<LightInfo> m_lightBufferHandle;
    DynamicStructuredBufferHandle<DirectX::XMMATRIX> m_spotViewInfoHandle;
    DynamicStructuredBufferHandle<DirectX::XMMATRIX> m_pointViewInfoHandle;
    DynamicStructuredBufferHandle<DirectX::XMMATRIX> m_directionalViewInfoHandle;
    //std::unordered_map<int, unsigned int> lightIndexMap; // Maps localID to buffer index
    std::vector<Light*> m_lights; // Active light IDs
    std::vector<Light*> m_spotLights;
	std::vector<Light*> m_pointLights;
	std::vector<Light*> m_directionalLights;

	std::unordered_map<int, DynamicGloballyIndexedResource> m_lightDrawSetBufferMap;
    unsigned int m_commandBufferSize;
	bool m_resizeCommandBuffers = false;
	static constexpr unsigned int m_commandBufferIncrementSize = 1000;

    // Settings funcs
	std::function<uint8_t()> getNumDirectionalLightCascades;
    std::function<std::vector<float>()> getDirectionalLightCascadeSplits;
    std::function<uint16_t()> getShadowResolution;
    std::function<ShadowMaps*()> getCurrentShadowMapResourceGroup;
    std::function<void(std::shared_ptr<void>)> markForDelete;

    Camera* m_currentCamera = nullptr;

    unsigned int CreateLightInfo(Light* node);
    unsigned int CreateLightViewInfo(Light* node, Camera* camera = nullptr);
    void UpdateLightViewInfo(Light* node);
	void RemoveLightViewInfo(Light* node);
    void CreateIndirectCommandBuffer(Light* light);
};
