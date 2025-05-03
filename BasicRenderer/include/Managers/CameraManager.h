#pragma once

#include <memory.h>
#include <mutex>
#include <atomic>
#include <flecs.h>

#include "ShaderBuffers.h"

#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"

class IndirectCommandBufferManager;

class CameraManager {
public:
	static std::unique_ptr<CameraManager> CreateUnique() {
		return std::unique_ptr<CameraManager>(new CameraManager());
	}

	unsigned int GetCameraBufferSRVIndex() const {
		return m_pCameraBuffer->GetSRVInfo()[0].index;
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

	void SetMeshletBitfieldSize(unsigned int numMeshlets);

private:
	CameraManager();
	std::shared_ptr<LazyDynamicStructuredBuffer<CameraInfo>> m_pCameraBuffer;
	std::mutex m_cameraUpdateMutex;
	std::atomic<uint64_t> m_viewIDCounter = 0;
	std::unordered_map<uint64_t, std::shared_ptr<DynamicGloballyIndexedResource>> m_meshletBitfieldBuffers;

	IndirectCommandBufferManager* m_pCommandBufferManager = nullptr;

	unsigned int m_currentMeshletBitfieldSize = 0;
};