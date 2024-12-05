#pragma once

#include <memory.h>

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

private:
	CameraManager();
	std::shared_ptr<LazyDynamicStructuredBuffer<CameraInfo>> m_pCameraBuffer;
};