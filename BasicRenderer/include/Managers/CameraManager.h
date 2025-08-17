#pragma once

#include <memory.h>
#include <mutex>
#include <atomic>
#include <flecs.h>

#include "ShaderBuffers.h"

#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Interfaces/IResourceProvider.h"
#include "Resources/ResourceIdentifier.h"

class IndirectCommandBufferManager;
class ResourceGroup;

class CameraManager : public IResourceProvider {
public:
	static std::unique_ptr<CameraManager> CreateUnique() {
		return std::unique_ptr<CameraManager>(new CameraManager());
	}

	Components::RenderView AddCamera(CameraInfo& camera);
	void RemoveCamera(Components::RenderView);

	void UpdateCamera(const Components::RenderView& view, CameraInfo& camera) {
		m_pCameraBuffer->UpdateView(view.cameraBufferView.get(), &camera);
	}

	std::shared_ptr<LazyDynamicStructuredBuffer<CameraInfo>>& GetCameraBuffer() {
		return m_pCameraBuffer;
	}

	void UpdatePerCameraBufferView(BufferView* view, CameraInfo& data) {
		std::lock_guard<std::mutex> lock(m_cameraUpdateMutex);
		m_pCameraBuffer->UpdateView(view, &data);
	}

	void SetCommandBufferManager(IndirectCommandBufferManager* commandBufferManager);

	void SetMeshletBitfieldSize(uint64_t numMeshlets);
	const std::shared_ptr<ResourceGroup>& GetMeshletCullingBitfieldGroup() const {
		return m_meshletCullingBitfieldGroup;
	}

	void SetNumMeshInstances(unsigned int numMeshInstances);

	void SetDepthBufferForCamera(Components::RenderView view, Components::DepthMap, bool isArray = false, unsigned int arrayIndex = 0);

	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
	std::vector<ResourceIdentifier> GetSupportedKeys() override;

private:
	CameraManager();
	std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;
	std::shared_ptr<LazyDynamicStructuredBuffer<CameraInfo>> m_pCameraBuffer;
	std::mutex m_cameraUpdateMutex;
	std::atomic<uint32_t> m_viewIDCounter = 0;
	std::unordered_map<uint64_t, std::shared_ptr<DynamicGloballyIndexedResource>> m_meshletBitfieldBuffers;
	std::unordered_map<uint64_t, std::shared_ptr<DynamicGloballyIndexedResource>> m_meshInstanceMeshletCullingBitfieldBuffers;
	std::unordered_map<uint64_t, std::shared_ptr<DynamicGloballyIndexedResource>> m_meshInstanceOcclusionCullingBitfieldBuffers;

	IndirectCommandBufferManager* m_pCommandBufferManager = nullptr;
	std::shared_ptr<ResourceGroup> m_meshletCullingBitfieldGroup;
	std::shared_ptr<ResourceGroup> m_meshInstanceMeshletCullingBitfieldGroup;
	std::shared_ptr<ResourceGroup> m_meshInstanceOcclusionCullingBitfieldGroup;

	uint64_t m_currentMeshletBitfieldSize = 1;
	uint32_t m_currentMeshInstanceBitfieldSize = 1;
};