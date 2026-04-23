#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Resources/Texture.h"

enum class TextureProcessingJobState : uint8_t {
	Queued = 0,
	Processing,
	Ready,
	Failed,
};

struct TextureProcessingJobHandle {
	std::atomic<TextureProcessingJobState> state = TextureProcessingJobState::Queued;
	std::mutex mutex;
	std::shared_ptr<TextureSourceData> result;
	bool loadedFromCache = false;
	std::string error;
};

class TextureProcessingManager {
public:
	static TextureProcessingManager& GetInstance();

	std::shared_ptr<TextureProcessingJobHandle> RequestProcessing(
		const std::shared_ptr<TextureSourceData>& sourceData,
		const TextureFileMeta& meta);

	bool ShouldProcess(const TextureFileMeta& meta) const;
	bool NeedsProcessing(const TextureSourceData& sourceData, const TextureFileMeta& meta) const;

private:
	TextureProcessingManager() = default;

	std::string BuildProcessingKey(
		const std::shared_ptr<TextureSourceData>& sourceData,
		const TextureFileMeta& meta) const;

	std::mutex m_mutex;
	std::unordered_map<std::string, std::weak_ptr<TextureProcessingJobHandle>> m_jobsByKey;
};