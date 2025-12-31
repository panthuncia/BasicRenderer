#pragma once

#include <rhi.h>
#include <vector>
#include <string>
#include <functional>
#include <memory>

#include "Resources/Resource.h"

struct ReadbackRequest {
    std::shared_ptr<Resource> readbackBuffer;
    std::vector<rhi::CopyableFootprint> layouts;
    UINT64 totalSize;
    std::wstring outputFile;
    std::function<void()> callback;
	UINT64 fenceValue;
};