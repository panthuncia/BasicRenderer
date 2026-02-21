#pragma once
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <string>
#include <rhi.h>

#include "Resources/ReadbackRequest.h"

class Resource;

struct ReadbackCaptureInfo {
	std::string passName;
	std::weak_ptr<Resource> resource;
	RangeSpec range{};
	ReadbackCaptureCallback callback;
};

struct ReadbackCaptureToken {
	uint64_t id = 0;
};

class ReadbackManager {
public:
	static ReadbackManager& GetInstance();

	void Initialize(rhi::Timeline readbackFence) {
		m_readbackFence = readbackFence;
	}

	void RequestReadbackCapture(
		const std::string& passName,
		Resource* resource,
		const RangeSpec& range,
		ReadbackCaptureCallback callback);

	std::vector<ReadbackCaptureInfo> ConsumeCaptureRequests();

	ReadbackCaptureToken EnqueueCapture(ReadbackCaptureRequest&& request);
	void FinalizeCapture(ReadbackCaptureToken token, uint64_t fenceValue);

	uint64_t GetNextReadbackFenceValue();
	rhi::Timeline GetReadbackFence() const { return m_readbackFence; }

	void ProcessReadbackRequests();

	void Cleanup() {
		m_queuedCaptures.clear();
		m_readbackCaptureRequests.clear();
	}

private:
	ReadbackManager() = default;

	rhi::Timeline m_readbackFence;
	std::mutex readbackRequestsMutex;
	std::vector<ReadbackCaptureRequest> m_readbackCaptureRequests;

	std::mutex m_captureQueueMutex;
	std::vector<ReadbackCaptureInfo> m_queuedCaptures;
	std::atomic<uint64_t> m_captureTokenCounter = 0;
	std::atomic<uint64_t> m_captureFenceValue = 0;

	// Static pointer to hold the instance
	static std::unique_ptr<ReadbackManager> instance;
	// Static initialization flag
	static bool initialized;
};

inline ReadbackManager& ReadbackManager::GetInstance() {
	if (!initialized) {
		instance = std::unique_ptr<ReadbackManager>(new ReadbackManager());
		initialized = true;
	}
	return *instance;
}
