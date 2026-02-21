#pragma once

#include <optional>
#include <rhi_allocator.h>

struct TextureAliasPlacement {
	rhi::ma::Allocation* allocation = nullptr;
	uint64_t offset = 0;
	std::optional<uint64_t> poolID;
};

struct BufferAliasPlacement {
	rhi::ma::Allocation* allocation = nullptr;
	uint64_t offset = 0;
	std::optional<uint64_t> poolID;
};
