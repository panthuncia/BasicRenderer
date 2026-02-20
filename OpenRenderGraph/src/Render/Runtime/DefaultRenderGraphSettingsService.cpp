#include "Render/Runtime/IRenderGraphSettingsService.h"

#include "Render/Runtime/OpenRenderGraphSettings.h"

namespace rg::runtime {

namespace {
class DefaultRenderGraphSettingsService final : public IRenderGraphSettingsService {
public:
    bool GetUseAsyncCompute() const override {
        return GetOpenRenderGraphSettings().useAsyncCompute;
    }

    uint8_t GetAutoAliasMode() const override {
        return GetOpenRenderGraphSettings().autoAliasMode;
    }

    uint8_t GetAutoAliasPackingStrategy() const override {
        return GetOpenRenderGraphSettings().autoAliasPackingStrategy;
    }

    bool GetAutoAliasLogExclusionReasons() const override {
        return GetOpenRenderGraphSettings().autoAliasLogExclusionReasons;
    }

    uint32_t GetAutoAliasPoolRetireIdleFrames() const override {
        return GetOpenRenderGraphSettings().autoAliasPoolRetireIdleFrames;
    }

    float GetAutoAliasPoolGrowthHeadroom() const override {
        return GetOpenRenderGraphSettings().autoAliasPoolGrowthHeadroom;
    }
};
}

std::shared_ptr<IRenderGraphSettingsService> CreateDefaultRenderGraphSettingsService() {
    return std::make_shared<DefaultRenderGraphSettingsService>();
}

}
