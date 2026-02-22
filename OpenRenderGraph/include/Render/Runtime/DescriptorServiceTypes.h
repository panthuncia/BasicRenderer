#pragma once

#include <cstdint>
#include <variant>

#include <rhi.h>

namespace rg::runtime {

struct DescriptorViewRequirements {
    struct TextureViews {
        uint32_t mipLevels = 1;
        bool isCubemap = false;
        bool isArray = false;
        uint32_t arraySize = 1;
        uint32_t totalArraySlices = 1;

        rhi::Format baseFormat = rhi::Format::Unknown;
        rhi::Format srvFormat = rhi::Format::Unknown;
        rhi::Format uavFormat = rhi::Format::Unknown;
        rhi::Format rtvFormat = rhi::Format::Unknown;
        rhi::Format dsvFormat = rhi::Format::Unknown;

        bool createSRV = true;
        bool createUAV = false;
        bool createNonShaderVisibleUAV = false;
        bool createRTV = false;
        bool createDSV = false;

        bool createCubemapAsArraySRV = false;
        uint32_t uavFirstMip = 0;
    };

    struct BufferViews {
        bool createCBV = false;
        bool createSRV = false;
        bool createUAV = false;
        bool createNonShaderVisibleUAV = false;

        rhi::CbvDesc cbvDesc{};
        rhi::SrvDesc srvDesc{};
        rhi::UavDesc uavDesc{};

        uint64_t uavCounterOffset = 0;
    };

    std::variant<TextureViews, BufferViews> views;
};

} // namespace rg::runtime
