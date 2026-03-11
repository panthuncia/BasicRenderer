#pragma once

#include <cstdint>
#include <memory>

#include <rhi.h>

#include "Mesh/ClusterLODTypes.h"
#include "Resources/Buffers/Buffer.h"
#include "ShaderBuffers.h"

inline constexpr const char* CLodStreamingMeshManagerGetterSettingName = "getMeshManager";
inline constexpr const char* CLodStreamingCpuUploadBudgetSettingName = "clodStreamingCpuUploadBudgetRequests";

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

/// Estimates the packed blob size (bytes) for a group from its chunk hints,
/// mirroring the alignment logic of PackGroupPayloadForPagePool.
inline size_t CLodEstimateBlobSize(
    const ClusterLODRuntimeSummary::GroupChunkHint& hint,
    uint32_t vertexByteSize)
{
    auto align4 = [](size_t v) -> size_t { return (v + 3u) & ~size_t(3); };

    size_t size = 0;
    size = align4(size) + hint.groupVertexCount * vertexByteSize;             // 1. vertex data
    size = align4(size) + hint.meshletVertexCount * sizeof(uint32_t);         // 2. meshlet vertex indices
    size = align4(size) + hint.meshletTrianglesByteCount * sizeof(uint8_t);   // 3. meshlet triangles
    size = align4(size) + hint.compressedPositionWordCount * sizeof(uint32_t);// 4. compressed positions
    size = align4(size) + hint.compressedNormalWordCount * sizeof(uint32_t);  // 5. compressed normals
    size = align4(size) + hint.compressedMeshletVertexWordCount * sizeof(uint32_t); // 6. compressed meshlet verts
    size = align4(size) + hint.meshletCount * sizeof(meshopt_Meshlet);        // 7. meshlet offsets
    size = align4(size) + hint.meshletBoundsCount * sizeof(BoundingSphere);   // 8. meshlet bounds
    return align4(size);
}

/// Returns the number of pages needed to hold a group's packed blob.
inline uint32_t CLodEstimatePagesNeeded(
    const ClusterLODRuntimeSummary::GroupChunkHint& hint,
    uint32_t vertexByteSize,
    uint64_t pageSize)
{
    const size_t blobSize = CLodEstimateBlobSize(hint, vertexByteSize);
    return static_cast<uint32_t>((blobSize + pageSize - 1) / pageSize);
}
