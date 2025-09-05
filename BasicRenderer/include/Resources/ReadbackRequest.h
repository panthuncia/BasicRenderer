#pragma once

#include <rhi.h>
#include <vector>
#include <string>
#include <functional>

struct ReadbackRequest {
    rhi::ResourceHandle readbackBuffer;
    std::vector<rhi::CopyableFootprint> layouts;
    UINT64 totalSize;
    std::wstring outputFile;
    std::function<void()> callback;
	UINT64 fenceValue;
};