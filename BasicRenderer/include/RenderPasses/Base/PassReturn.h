#pragma once

#include <rhi.h>
#include <optional>
#include <cstdint>

struct PassReturn {
	std::optional<rhi::Timeline> fence;
	uint64_t fenceValue = 0;
};
