#pragma once

#include <rhi.h>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <cstddef>

#include "Resources/Resource.h"
#include "Resources/ResourceStateTracker.h"

struct ReadbackRequest {
    std::shared_ptr<Resource> readbackBuffer;
    std::vector<rhi::CopyableFootprint> layouts;
    UINT64 totalSize;
    std::wstring outputFile;
    std::function<void()> callback;
	UINT64 fenceValue;
};

enum class ReadbackResourceKind : uint8_t {
    Buffer,
    Texture
};

struct ReadbackCaptureDesc {
    ReadbackResourceKind kind = ReadbackResourceKind::Buffer;
    uint64_t resourceId = 0;
    RangeSpec range;
};

struct ReadbackCaptureResult {
    ReadbackCaptureDesc desc;
    std::vector<rhi::CopyableFootprint> layouts;
    std::vector<std::byte> data;
    rhi::Format format = rhi::Format::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
};

using ReadbackCaptureCallback = std::function<void(ReadbackCaptureResult&&)>;

struct ReadbackCaptureRequest {
    uint64_t token = 0;
    ReadbackCaptureDesc desc;
    std::shared_ptr<Resource> readbackBuffer;
    std::vector<rhi::CopyableFootprint> layouts;
    uint64_t totalSize = 0;
    rhi::Format format = rhi::Format::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    ReadbackCaptureCallback callback;
    uint64_t fenceValue = 0;
};