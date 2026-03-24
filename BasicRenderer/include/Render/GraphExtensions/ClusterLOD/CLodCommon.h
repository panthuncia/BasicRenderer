#pragma once

#include <cstdint>
#include <memory>

#include <rhi.h>

#include "Mesh/ClusterLODTypes.h"
#include "Resources/Buffers/Buffer.h"
#include "ShaderBuffers.h"

inline constexpr const char* CLodStreamingMeshManagerGetterSettingName = "getMeshManager";
inline constexpr const char* CLodStreamingCpuUploadBudgetSettingName = "clodStreamingCpuUploadBudgetRequests";

enum class CLodPriorityMode : uint8_t {
    Max, // Duplicate group requests keep the maximum reported priority
    Sum, // Duplicate group requests accumulate (sum) their priorities
};

enum class CLodSoftwareRasterMode : uint8_t {
    Disabled,
    Compute,
    WorkGraph,
};

enum class CLodRasterOutputKind : uint8_t {
    VisibilityBuffer,
    DeepVisibility,
};

inline constexpr const char* CLodSoftwareRasterModeSettingName = "clodSoftwareRasterMode";
inline constexpr const char* CLodSoftwareRasterModeNames[] = {
    "Disabled",
    "Compute",
    "Work Graph",
};
inline constexpr int CLodSoftwareRasterModeCount = static_cast<int>(sizeof(CLodSoftwareRasterModeNames) / sizeof(CLodSoftwareRasterModeNames[0]));

constexpr bool CLodSoftwareRasterEnabled(CLodSoftwareRasterMode mode)
{
    return mode != CLodSoftwareRasterMode::Disabled;
}

constexpr bool CLodSoftwareRasterUsesCompute(CLodSoftwareRasterMode mode)
{
    return mode == CLodSoftwareRasterMode::Compute;
}

constexpr bool CLodSoftwareRasterUsesWorkGraph(CLodSoftwareRasterMode mode)
{
    return mode == CLodSoftwareRasterMode::WorkGraph;
}

struct RasterBucketsHistogramIndirectCommand
{
    unsigned int clusterCount;
    unsigned int dispatchXDimension;
    unsigned int dispatchX, dispatchY, dispatchZ;
};

struct RasterizeClustersCommand
{
    unsigned int baseClusterOffset;
    unsigned int xDim;
    unsigned int rasterBucketID;
    unsigned int dispatchX, dispatchY, dispatchZ;
};

struct CLodViewRasterInfo
{
    uint32_t visibilityUAVDescriptorIndex;
    uint32_t scissorMinX;
    uint32_t scissorMinY;
    uint32_t scissorMaxX;
    uint32_t scissorMaxY;
    float viewportScaleX;
    float viewportScaleY;
    uint32_t pad0;

    friend bool operator==(const CLodViewRasterInfo&, const CLodViewRasterInfo&) = default;
};

inline constexpr uint32_t CLodReplayBufferSizeBytes = 200u * 1024u * 1024u; // 200 MB physical, GPU uses first 100 MB
inline constexpr uint32_t CLodReplayNodeRegionSizeBytes = 50u * 1024u * 1024u;    // must match HLSL CLOD_REPLAY_NODE_REGION_SIZE_BYTES
inline constexpr uint32_t CLodReplayMeshletRegionOffset = CLodReplayNodeRegionSizeBytes;
inline constexpr uint32_t CLodNodeReplayStrideBytes = 12u;   // sizeof(TraverseNodeRecord): 3 uints
inline constexpr uint32_t CLodMeshletReplayStrideBytes = 24u; // sizeof(MeshletBucketRecord): 6 uints
inline constexpr uint32_t CLodReplayBufferNumUints = CLodReplayBufferSizeBytes / sizeof(uint32_t);
inline constexpr uint32_t CLodMaxViewDepthIndices = 512u;
inline constexpr uint32_t CLodStreamingInitialGroupCapacity = 1024u;
inline constexpr uint32_t CLodStreamingRequestCapacity = (1u << 16);
inline constexpr uint32_t CLodUsedGroupsCapacity = (1u << 17); // 128K entries for GPU used-groups append buffer

constexpr uint32_t CLodBitsetWordCount(uint32_t bitCount)
{
    return (bitCount + 31u) / 32u;
}

inline uint32_t CLodRoundUpCapacity(uint32_t required)
{
    uint32_t capacity = CLodStreamingInitialGroupCapacity;
    while (capacity < required) {
        capacity *= 2u;
    }
    return capacity;
}

inline std::shared_ptr<Buffer> CreateAliasedUnmaterializedStructuredBuffer(
    uint32_t numElements,
    uint32_t elementSize,
    bool unorderedAccess = true,
    bool unorderedAccessCounter = false,
    bool createNonShaderVisibleUAV = false,
    bool allowAlias = true)
{
    auto buffer = Buffer::CreateUnmaterializedStructuredBuffer(
        numElements,
        elementSize,
        unorderedAccess,
        unorderedAccessCounter,
        createNonShaderVisibleUAV,
        rhi::HeapType::DeviceLocal);
    buffer->SetAllowAlias(allowAlias);
    return buffer;
}

inline std::shared_ptr<Buffer> CreateAliasedUnmaterializedRawBuffer(
    uint64_t bufferSizeBytes,
    bool unorderedAccess = true,
    bool createNonShaderVisibleUAV = false,
    bool allowAlias = true)
{
    if (bufferSizeBytes == 0 || (bufferSizeBytes % 4u) != 0u) {
        throw std::runtime_error("Raw buffer requires a non-zero byte size that is divisible by 4");
    }

    auto buffer = Buffer::CreateSharedUnmaterialized(rhi::HeapType::DeviceLocal, bufferSizeBytes, unorderedAccess);

    BufferBase::DescriptorRequirements requirements{};
    requirements.createSRV = true;
    requirements.createUAV = unorderedAccess;
    requirements.createNonShaderVisibleUAV = unorderedAccess && createNonShaderVisibleUAV;
    requirements.uavCounterOffset = 0;
    requirements.srvDesc = rhi::SrvDesc{
        .dimension = rhi::SrvDim::Buffer,
        .formatOverride = rhi::Format::R32_Typeless,
        .buffer = {
            .kind = rhi::BufferViewKind::Raw,
            .firstElement = 0,
            .numElements = static_cast<uint32_t>(bufferSizeBytes / 4u),
            .structureByteStride = 0,
        },
    };
    requirements.uavDesc = rhi::UavDesc{
        .dimension = rhi::UavDim::Buffer,
        .formatOverride = rhi::Format::R32_Typeless,
        .buffer = {
            .kind = rhi::BufferViewKind::Raw,
            .firstElement = 0,
            .numElements = static_cast<uint32_t>(bufferSizeBytes / 4u),
            .structureByteStride = 0,
            .counterOffsetInBytes = 0,
        },
    };

    buffer->SetDescriptorRequirements(requirements);
    buffer->SetAllowAlias(allowAlias);
    return buffer;
}

// Returns the number of pages needed for a group.
// Uses the physical page count set during the build pipeline.
inline uint32_t CLodEstimatePagesNeeded(
    const ClusterLODRuntimeSummary::GroupChunkHint& hint,
    uint32_t /*vertexByteSize*/,
    uint64_t /*pageSize*/)
{
    return hint.pageCount;
}
