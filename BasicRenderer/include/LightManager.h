#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>
#include <memory>
#include "buffers.h"
#include "Interfaces/ISceneNodeObserver.h"
#include "DynamicResource.h"
#include "LazyDynamicStructuredBuffer.h"
#include "DynamicStructuredBuffer.h"
#include "Components.h"

class ShadowMaps;
class IndirectCommandBufferManager;
class CameraManager;
class SortedUnsignedIntBuffer;

class LightManager {
public:
	static std::unique_ptr<LightManager> CreateUnique() {
		return std::unique_ptr<LightManager>(new LightManager());
	}
    ~LightManager();
    std::pair<Components::LightViewInfo, std::optional<Components::ShadowMap>> AddLight(LightInfo* lightInfo, uint64_t entityId);
    void RemoveLight(LightInfo* light);
    unsigned int GetLightBufferDescriptorIndex();
	unsigned int GetActiveLightIndicesBufferDescriptorIndex();
    unsigned int GetPointCubemapMatricesDescriptorIndex();
    unsigned int GetSpotMatricesDescriptorIndex();
    unsigned int GetDirectionalCascadeMatricesDescriptorIndex();
    unsigned int GetNumLights();
    void SetCurrentCamera(flecs::entity camera);
	void SetCommandBufferManager(IndirectCommandBufferManager* commandBufferManager);
	void SetCameraManager(CameraManager* cameraManager);


private:
    LightManager();
	flecs::entity m_currentCamera;
    std::shared_ptr<LazyDynamicStructuredBuffer<LightInfo>> m_lightBuffer;
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeLightIndices; // Sorted list of active light indices
    std::shared_ptr<DynamicStructuredBuffer<unsigned int>> m_spotViewInfo; // Indices into camera buffer
    std::shared_ptr<DynamicStructuredBuffer<unsigned int>> m_pointViewInfo;
    std::shared_ptr<DynamicStructuredBuffer<unsigned int>> m_directionalViewInfo;

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

    Components::LightViewInfo CreateLightViewInfo(LightInfo info, uint64_t entityId);
    void UpdateLightViewInfo(flecs::entity light);
	void RemoveLightViewInfo(flecs::entity light);
};
