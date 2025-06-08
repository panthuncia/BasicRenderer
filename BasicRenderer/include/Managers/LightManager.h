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
#include "Interfaces/IResourceProvider.h"

class ShadowMaps;
class LinearShadowMaps;
class IndirectCommandBufferManager;
class CameraManager;
class SortedUnsignedIntBuffer;

struct AddLightReturn {
	Components::LightViewInfo lightViewInfo;
	std::optional<Components::DepthMap> shadowMap;
	std::optional<Components::FrustrumPlanes> frustrumPlanes;
};

class LightManager: public IResourceProvider {
public:
	static std::unique_ptr<LightManager> CreateUnique() {
		return std::unique_ptr<LightManager>(new LightManager());
	}
    ~LightManager();
    AddLightReturn AddLight(LightInfo* lightInfo, uint64_t entityId);
    void RemoveLight(LightInfo* light);
    unsigned int GetNumLights();
    void SetCurrentCamera(flecs::entity camera);
	void SetCameraManager(CameraManager* cameraManager);
	void UpdateLightBufferView(BufferView* view, LightInfo& data);
    void UpdateLightViewInfo(flecs::entity light);
	unsigned int GetLightPagePoolSize() { return m_lightPagePoolSize; }
	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
	std::vector<ResourceIdentifier> GetSupportedKeys() override;

private:
    LightManager();
	std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;
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
	std::function<LinearShadowMaps*()> getCurrentLinearShadowMapResourceGroup;
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
