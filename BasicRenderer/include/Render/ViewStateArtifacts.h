#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Render/AsyncStateGraph.h"
#include "Scene/Components.h"

namespace org { class PixelBuffer; class Resource; }

namespace br::render {

struct DepthHistorySelection {
    std::shared_ptr<org::PixelBuffer> resource;
    std::uint64_t epoch = 0;
    std::uint64_t producerSubmissionID = 0;

    explicit operator bool() const noexcept {
        return resource != nullptr && producerSubmissionID != 0;
    }
};

struct PreparedViewFrameData {
    std::uint64_t id = 0;
    std::uint32_t cameraBufferIndex = 0;
    bool primary = false;
    bool shadow = false;
    bool cascade = false;
    Components::LightType lightType = Components::LightType::Directional;
    CameraInfo cameraInfo{};
    std::shared_ptr<org::PixelBuffer> visibilityBuffer;
    std::shared_ptr<org::PixelBuffer> deepVisibilityHeadPointers;
    std::shared_ptr<org::PixelBuffer> linearDepthMap;
    DepthHistorySelection depthHistory;
    std::vector<std::uint32_t> linearDepthSRVIndices;
    std::int32_t depthBufferArrayIndex = -1;
    std::uint32_t visibilitySRVIndex = 0xFFFFFFFFu;
    std::uint32_t visibilityUAVIndex = 0xFFFFFFFFu;
    std::uint32_t deepVisibilityHeadPointersUAVIndex = 0xFFFFFFFFu;
};

struct ViewFamilyBuildInput {
    std::uint64_t revision = 0;
    std::uint32_t cameraBufferSize = 0;
    std::uint64_t resourceLayoutRevision = 0;
    std::vector<PreparedViewFrameData> views;
    std::vector<std::shared_ptr<org::Resource>> retainedResources;
};

struct PublishedViewFamilyState {
    std::uint64_t revision = 0;
    std::uint32_t cameraBufferSize = 0;
    std::uint64_t resourceLayoutRevision = 0;
    std::vector<PreparedViewFrameData> views;
    std::vector<std::shared_ptr<org::Resource>> retainedResources;
};

void RegisterViewStateProducer(AsyncStateGraph& graph);

} // namespace br::render
