#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

#include "ShaderBuffers.h"
#include "Resources/DynamicResource.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"
#include "Scene/Components.h"

class ShadowMaps;
class IndirectCommandBufferManager;
class CameraManager;
class SortedUnsignedIntBuffer;

struct AddLightReturn {
	Components::LightViewInfo lightViewInfo;
	std::optional<Components::ShadowMap> shadowMap;
	std::optional<Components::FrustrumPlanes> frustrumPlanes;
};

class LightManager {
public:
	static std::unique_ptr<LightManager> CreateUnique() {
		return std::unique_ptr<LightManager>(new LightManager());
	}
    ~LightManager();
    AddLightReturn AddLight(LightInfo* lightInfo, uint64_t entityId);
    void RemoveLight(LightInfo* light);
    unsigned int GetLightBufferDescriptorIndex();
	unsigned int GetActiveLightIndicesBufferDescriptorIndex();
    unsigned int GetPointCubemapMatricesDescriptorIndex();
    unsigned int GetSpotMatricesDescriptorIndex();
    unsigned int GetDirectionalCascadeMatricesDescriptorIndex();
    unsigned int GetNumLights();
    void SetCurrentCamera(flecs::entity camera);
	void SetCameraManager(CameraManager* cameraManager);
	void UpdateLightBufferView(BufferView* view, LightInfo& data);
    void UpdateLightViewInfo(flecs::entity light);
	std::shared_ptr<ResourceGroup>& GetLightViewInfoResourceGroup();
	std::shared_ptr<ResourceGroup>& GetLightBufferResourceGroup();
	std::shared_ptr<Buffer>& GetClusterBuffer();
	std::shared_ptr<Buffer>& GetLightPagesBuffer();
	unsigned int GetLightPagePoolSize() { return m_lightPagePoolSize; }

private:
    LightManager();
	flecs::entity m_currentCamera;
    std::shared_ptr<LazyDynamicStructuredBuffer<LightInfo>> m_lightBuffer;
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeLightIndices; // Sorted list of active light indices
    std::shared_ptr<DynamicStructuredBuffer<unsigned int>> m_spotViewInfo; // Indices into camera buffer
    std::shared_ptr<DynamicStructuredBuffer<unsigned int>> m_pointViewInfo;
    std::shared_ptr<DynamicStructuredBuffer<unsigned int>> m_directionalViewInfo;

	std::shared_ptr<ResourceGroup> m_pLightViewInfoResourceGroup;
	std::shared_ptr<ResourceGroup> m_pLightBufferResourceGroup;

	std::shared_ptr<Buffer> m_pClusterBuffer;
	std::shared_ptr<Buffer> m_pLightPagesBuffer;

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
    CameraManager* m_pCameraManager = nullptr;
	unsigned int m_lightPagePoolSize = 0;

	std::mutex m_lightUpdateMutex;

    std::pair<Components::LightViewInfo, std::optional<Components::FrustrumPlanes>>
        CreatePointLightViewInfo(const LightInfo& info, uint64_t entityId);
	std::pair<Components::LightViewInfo, std::optional<Components::FrustrumPlanes>>
		CreateSpotLightViewInfo(const LightInfo& info, uint64_t entityId);
	std::pair<Components::LightViewInfo, std::optional<Components::FrustrumPlanes>>
		CreateDirectionalLightViewInfo(const LightInfo& info, uint64_t entityId);

	void RemoveLightViewInfo(flecs::entity light);
};
