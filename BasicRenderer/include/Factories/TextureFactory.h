#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <string_view>

#include "Resources/TextureDescription.h"

class PixelBuffer;
class Sampler;

// Central API for textures that have initial texel data.
class TextureFactory {
public:
    static std::unique_ptr<TextureFactory> CreateUnique() {
		return std::unique_ptr<TextureFactory>(new TextureFactory());
    }
    // Owned initial texel bytes for a texture creation request.
	// Subresource order is: [slice0 mip0..mipN-1, slice1 ...].
    struct TextureInitialData {
        std::vector<std::shared_ptr<std::vector<uint8_t>>> subresources;

        bool Empty() const noexcept { return subresources.empty(); }

        static TextureInitialData FromBytes(const std::vector<std::shared_ptr<std::vector<uint8_t>>>& bytes) {
            TextureInitialData d;
            d.subresources = bytes;
            return d;
        }
    };

    std::shared_ptr<PixelBuffer> CreateAlwaysResidentPixelBuffer(
        TextureDescription desc,
        TextureInitialData initialData,
        std::string_view debugName = {}) const;

private:
    TextureFactory() = default;
};
