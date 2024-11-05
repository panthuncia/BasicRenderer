#pragma once

#include <memory.h>

#include "buffers.h"
#include "DynamicStructuredBuffer.h"

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

private:
	CameraManager();
	std::shared_ptr<DynamicStructuredBuffer<CameraInfo>> m_pCameraBuffer;
};