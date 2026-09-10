#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Render/AsyncStateGraph.h"

class Skeleton;
namespace org { class Resource; }

namespace br::render {

struct PublishedSkeletonInstance {
    std::shared_ptr<const Skeleton> baseSkeleton;
    std::uint32_t instanceSlot = 0xFFFFFFFFu;
    std::uint32_t transformOffsetMatrices = 0;
    std::uint32_t inverseSkinOffsetMatrices = 0;
    std::uint32_t boneCount = 0;
};

struct PoseStateBuildInput {
    std::uint64_t activeInstanceRevision = 0;
    std::vector<PublishedSkeletonInstance> activeInstances;
    std::vector<std::shared_ptr<org::Resource>> retainedResources;
};

struct PublishedPoseState {
    std::uint64_t activeInstanceRevision = 0;
    std::vector<PublishedSkeletonInstance> activeInstances;
    std::vector<std::shared_ptr<org::Resource>> retainedResources;
};

void RegisterPoseStateProducer(AsyncStateGraph& graph);

} // namespace br::render
