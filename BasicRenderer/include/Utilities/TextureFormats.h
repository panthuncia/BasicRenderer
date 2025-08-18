#pragma once

#include <ktx.h>
#include <vulkan/vulkan.h>


inline ktx_transcode_fmt_e ChooseBasisTranscodeTarget(bool preferSrgb) {
	return preferSrgb ? KTX_TTF_BC7_RGBA : KTX_TTF_BC7_RGBA;
}

inline DXGI_FORMAT MapVkToDxgi(VkFormat vk) {
	switch (vk) {
	case VK_FORMAT_R8_UNORM:                 return DXGI_FORMAT_R8_UNORM;
	case VK_FORMAT_R8G8_UNORM:               return DXGI_FORMAT_R8G8_UNORM;
	case VK_FORMAT_R8G8B8A8_UNORM:           return DXGI_FORMAT_R8G8B8A8_UNORM;
	case VK_FORMAT_R8G8B8A8_SRGB:            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case VK_FORMAT_B8G8R8A8_UNORM:           return DXGI_FORMAT_B8G8R8A8_UNORM;
	case VK_FORMAT_B8G8R8A8_SRGB:            return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

	case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:     return DXGI_FORMAT_BC1_UNORM;
	case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:      return DXGI_FORMAT_BC1_UNORM_SRGB;

	case VK_FORMAT_BC3_UNORM_BLOCK:          return DXGI_FORMAT_BC3_UNORM;
	case VK_FORMAT_BC3_SRGB_BLOCK:           return DXGI_FORMAT_BC3_UNORM_SRGB;

	case VK_FORMAT_BC4_UNORM_BLOCK:          return DXGI_FORMAT_BC4_UNORM;
	case VK_FORMAT_BC4_SNORM_BLOCK:          return DXGI_FORMAT_BC4_SNORM;

	case VK_FORMAT_BC5_UNORM_BLOCK:          return DXGI_FORMAT_BC5_UNORM;
	case VK_FORMAT_BC5_SNORM_BLOCK:          return DXGI_FORMAT_BC5_SNORM;

	case VK_FORMAT_BC7_UNORM_BLOCK:          return DXGI_FORMAT_BC7_UNORM;
	case VK_FORMAT_BC7_SRGB_BLOCK:           return DXGI_FORMAT_BC7_UNORM_SRGB;

		// Add more as you encounter them.
	default:                                 return DXGI_FORMAT_UNKNOWN;
	}
}

// Map BasisU transcode target to DXGI
inline DXGI_FORMAT MapKtxTranscodedToDxgi(ktx_transcode_fmt_e tfmt, bool preferSrgb) {
	switch (tfmt) {
	case KTX_TTF_BC1_RGB:        return preferSrgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
	case KTX_TTF_BC3_RGBA:       return preferSrgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
	case KTX_TTF_BC4_R:          return DXGI_FORMAT_BC4_UNORM;
	case KTX_TTF_BC5_RG:         return DXGI_FORMAT_BC5_UNORM;
	case KTX_TTF_BC7_RGBA:       return preferSrgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
	case KTX_TTF_RGBA32:         return DXGI_FORMAT_R8G8B8A8_UNORM; // libktx may give 8-bit RGBA when using RGBA32
	default:                     return DXGI_FORMAT_UNKNOWN;
	}
}

inline uint32_t ComputeRowPitch(DXGI_FORMAT fmt, uint32_t width) {
	auto isBC = [](DXGI_FORMAT f) {
		switch (f) {
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB: return true;
		default: return false;
		}
		};

	if (isBC(fmt)) {
		const uint32_t blockBytes =
			(fmt == DXGI_FORMAT_BC1_UNORM || fmt == DXGI_FORMAT_BC1_UNORM_SRGB ||
				fmt == DXGI_FORMAT_BC4_UNORM || fmt == DXGI_FORMAT_BC4_SNORM) ? 8u : 16u;
		const uint32_t blocksWide = std::max(1u, (width + 3u) / 4u);
		return blocksWide * blockBytes;
	}
	return width * (DXGI_FORMAT_R8_UNORM == fmt ? 1 : 4); // R8 vs RGBA8
}