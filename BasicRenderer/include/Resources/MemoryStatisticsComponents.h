#pragma once

#include <cstdint>
#include <optional>

#include "rhi.h"

namespace MemoryStatisticsComponents
{
	struct MemSizeBytes {
		uint64_t size;
	};

	struct ResourceType {
		rhi::ResourceType type;
	};

	struct ResourceID {
		uint64_t id;
	};

	struct ResourceName {
		std::string name;
	};

	struct AliasingPool {
		std::optional<uint64_t> poolID;
	};
}