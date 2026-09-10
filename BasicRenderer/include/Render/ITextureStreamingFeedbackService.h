#pragma once

#include <memory>

class RenderPass;

// Narrow frame-graph boundary for texture-streaming feedback. The service owns
// suppression, readback reservations, and their completion handling.
class ITextureStreamingFeedbackService {
public:
	virtual ~ITextureStreamingFeedbackService() = default;
	virtual std::shared_ptr<RenderPass> CreateTextureStreamingFeedbackReadbackPass() = 0;
};
