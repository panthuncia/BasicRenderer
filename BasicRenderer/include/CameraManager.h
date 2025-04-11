#pragma once

#include <memory.h>
#include <mutex>

#include "buffers.h"

#include "LazyDynamicStructuredBuffer.h"

class CameraManager {
public:
	static std::unique_ptr<CameraManager> CreateUnique() {
		return std::unique_ptr<CameraManager>(new CameraManager());
	}

	unsigned int GetCameraBufferSRVIndex() const {
		return m_pCameraBuffer->GetSRVInfo().index;
	}

	std::shared_ptr<BufferView> AddCamera(CameraInfo& camera);
	void RemoveCamera(std::shared_ptr<BufferView> view);

	void UpdateCamera(std::shared_ptr<BufferView> view, CameraInfo& camera) {
		m_pCameraBuffer->UpdateView(view.get(), &camera);
	}

	std::shared_ptr<LazyDynamicStructuredBuffer<CameraInfo>>& GetCameraBuffer() {
		return m_pCameraBuffer;
	}

	void UpdatePerCameraBufferView(BufferView* view, CameraInfo& data) {
		std::lock_guard<std::mutex> lock(m_cameraUpdateMutex);
		m_pCameraBuffer->UpdateView(view, &data);
	}

private:
	CameraManager();
	std::shared_ptr<LazyDynamicStructuredBuffer<CameraInfo>> m_pCameraBuffer;
	std::mutex m_cameraUpdateMutex;
};