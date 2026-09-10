#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Render/AsyncStateGraph.h"
#include "Render/PublishedRendererState.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"
#include "Resources/Buffers/SortedUnsignedIntBuffer.h"
#include "ShaderBuffers.h"

namespace br::render {

struct PublishedGpuBufferVersion;

struct PublishedActiveSkinnedPlacement {
    std::uint32_t drawRecordIndex = 0;
    std::uint32_t generation = 0;
};

inline constexpr std::uint64_t kObjectPerObjectVariant = 1;
inline constexpr std::uint64_t kObjectInstanceTransformVariant = 2;
inline constexpr std::uint64_t kObjectDrawRecordVariant = 3;
inline constexpr std::uint64_t kObjectNormalMatrixVariant = 4;
inline constexpr std::uint64_t kObjectVisibilityGenerationVariant = 5;

struct ObjectBufferDependencyDTO {
    ArtifactKey key{ ArtifactKind::BufferVersion, 0, 0 };
    std::uint64_t revision = 0;
    std::uint32_t elementStride = 0;
    std::uint64_t catalogVariant = 0;
};

struct ObjectBufferStateBuildInput {
    std::vector<ObjectBufferDependencyDTO> buffers;
    std::uint64_t coveredMutationGeneration = 0;
    std::uint32_t residentTransformCount = 0;
    std::shared_ptr<DynamicStructuredBuffer<SkinnedAssemblyPlacementGPU>> skinnedPlacements;
    std::shared_ptr<SortedUnsignedIntBuffer> activeSkinnedPlacements;
    std::uint32_t activeSkinnedPlacementResidentSize = 0;
    std::shared_ptr<const std::vector<SkinnedAssemblyPlacementGPU>> placementRecords;
    std::shared_ptr<const std::vector<PublishedActiveSkinnedPlacement>> activePlacementEntries;
};

struct PublishedObjectBufferState {
    std::vector<ObjectBufferDependencyDTO> buffers;
    std::vector<std::shared_ptr<const PublishedGpuBufferVersion>> versions;
    std::uint64_t coveredMutationGeneration = 0;
    std::uint32_t residentTransformCount = 0;
    std::shared_ptr<DynamicStructuredBuffer<SkinnedAssemblyPlacementGPU>> skinnedPlacements;
    std::shared_ptr<SortedUnsignedIntBuffer> activeSkinnedPlacements;
    std::uint32_t activeSkinnedPlacementResidentSize = 0;
    std::shared_ptr<const std::vector<SkinnedAssemblyPlacementGPU>> placementRecords;
    std::shared_ptr<const std::vector<PublishedActiveSkinnedPlacement>> activePlacementEntries;
};

void RegisterObjectBufferStateProducer(AsyncStateGraph& graph);

} // namespace br::render
