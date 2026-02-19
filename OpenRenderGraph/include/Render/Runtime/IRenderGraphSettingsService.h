#pragma once

#include <cstdint>
#include <memory>

namespace rg::runtime {

class IRenderGraphSettingsService {
public:
    virtual ~IRenderGraphSettingsService() = default;

    virtual bool GetUseAsyncCompute() const = 0;
    virtual uint8_t GetAutoAliasMode() const = 0;
    virtual uint8_t GetAutoAliasPackingStrategy() const = 0;
    virtual bool GetAutoAliasLogExclusionReasons() const = 0;
    virtual uint32_t GetAutoAliasPoolRetireIdleFrames() const = 0;
    virtual float GetAutoAliasPoolGrowthHeadroom() const = 0;
};

std::shared_ptr<IRenderGraphSettingsService> CreateDefaultRenderGraphSettingsService();

}
