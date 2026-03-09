#pragma once

#include <cstdint>
#include <memory>

#include <rhi.h>

#include "Resources/Buffers/Buffer.h"
#include "ShaderBuffers.h"

inline constexpr const char* CLodStreamingMeshManagerGetterSettingName = "getMeshManager";
inline constexpr const char* CLodStreamingCpuUploadBudgetSettingName = "clodStreamingCpuUploadBudgetRequests";
inline constexpr const char* CLodStreamingResidentBudgetSettingName = "clodStreamingResidentBudgetGroups";

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

inline constexpr uint32_t CLodReplayBufferSizeBytes = 8u * 1024u * 1024u;
inline constexpr uint32_t CLodReplayBufferNumUints = CLodReplayBufferSizeBytes / sizeof(uint32_t);
inline constexpr uint32_t CLodMaxViewDepthIndices = 512u;
inline constexpr uint32_t CLodStreamingInitialGroupCapacity = 1024u;
inline constexpr uint32_t CLodStreamingRequestCapacity = (1u << 16);

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
