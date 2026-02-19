#pragma once

#include <cstdint>

namespace rg::shaderabi {
	inline constexpr uint32_t kResourceDescriptorIndicesRootParameter = 6;
	inline constexpr uint32_t kNumResourceDescriptorIndicesRootConstants = 35;

	inline constexpr uint32_t kIndirectCommandSignatureRootParameter = 7;
	inline constexpr uint32_t kNumIndirectCommandSignatureRootConstants = 4;
}
