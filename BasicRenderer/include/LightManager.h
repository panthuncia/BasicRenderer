#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>
#include <memory>
#include "buffers.h"
#include "Interfaces/ISceneNodeObserver.h"
#include "DynamicResource.h"
#include "DynamicStructuredBuffer.h"

class Camera;
class Light;
class ShadowMaps;
class SceneNode;
class IndirectCommandBufferManager;
class CameraManager;

class LightManager : public ISceneNodeObserver<Light>, public ISceneNodeObserver<SceneNode> {
public:
	static std::unique_ptr<LightManager> CreateUnique() {
		return std::unique_ptr<LightManager>(new LightManager());
	}
    ~LightManager();
    void AddLight(Light* lightNode, Camera* currentCamera = nullptr);
    void RemoveLight(Light* light);
    unsigned int GetLightBufferDescriptorIndex();
    unsigned int GetPointCubemapMatricesDescriptorIndex();
    unsigned int GetSpotMatricesDescriptorIndex();
    unsigned int GetDirectionalCascadeMatricesDescriptorIndex();
    unsigned int GetNumLights();
    void SetCurrentCamera(Camera* camera);
    void OnNodeUpdated(SceneNode* camera) override;
    void OnNodeUpdated(Light* light) override;
	void SetCommandBufferManager(IndirectCommandBufferManager* commandBufferManager);
	void SetCameraManager(CameraManager* cameraManager);


private:
    LightManager();
    std::shared_ptr<DynamicStructuredBuffer<LightInfo>> m_lightBuffer;
    std::shared_ptr<DynamicStructuredBuffer<unsigned int>> m_spotViewInfo; // Indices into camera buffer
    std::shared_ptr<DynamicStructuredBuffer<unsigned int>> m_pointViewInfo;
    std::shared_ptr<DynamicStructuredBuffer<unsigned int>> m_directionalViewInfo;
    std::vector<Light*> m_lights; // Active light IDs
    std::vector<Light*> m_spotLights;
	std::vector<Light*> m_pointLights;
	std::vector<Light*> m_directionalLights;

	std::unordered_map<int, DynamicGloballyIndexedResource> m_lightDrawSetBufferMap;
    // TODO: The buffer size and increment size are low for testing.
    unsigned int m_commandBufferSize = 1;
	bool m_resizeCommandBuffers = false;
	static constexpr unsigned int m_commandBufferIncrementSize = 1;

    // Settings funcs
	std::function<uint8_t()> getNumDirectionalLightCascades;
    std::function<std::vector<float>()> getDirectionalLightCascadeSplits;
    std::function<uint16_t()> getShadowResolution;
    std::function<ShadowMaps*()> getCurrentShadowMapResourceGroup;
    std::function<void(std::shared_ptr<void>)> markForDelete;
	IndirectCommandBufferManager* m_pCommandBufferManager = nullptr;
    CameraManager* m_pCameraManager = nullptr;

    Camera* m_currentCamera = nullptr;

    unsigned int CreateLightInfo(Light* node);
    unsigned int CreateLightViewInfo(Light* node, Camera* camera = nullptr);
    void UpdateLightViewInfo(Light* node);
	void RemoveLightViewInfo(Light* node);
};
