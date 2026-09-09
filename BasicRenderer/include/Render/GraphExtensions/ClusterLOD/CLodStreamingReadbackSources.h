#pragma once

#include <memory>
#include "Resources/Buffers/Buffer.h"

// Inputs identifying the GPU source buffers for the streaming readback copy.
struct CLodStreamingReadbackSources {
    std::shared_ptr<Buffer> counterSource;       // GPU load counter (1 × uint32)
    std::shared_ptr<Buffer> requestsSource;      // GPU load requests (N × CLodStreamingRequest)
    std::shared_ptr<Buffer> usedGroupsCounterSource; // GPU used-groups counter (1 × uint32)
    std::shared_ptr<Buffer> usedGroupsBufferSource;  // GPU used-groups buffer (N × uint32)
    std::shared_ptr<Buffer> sourceGroupMismatchCounterSource;
    std::shared_ptr<Buffer> sourceGroupMismatchDetailsSource;
    std::shared_ptr<Buffer> virtualShadowDependencyCountSource;
    std::shared_ptr<Buffer> virtualShadowDependenciesSource;

};

