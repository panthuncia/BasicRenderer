#include "Render/Runtime/IRenderGraphSettingsService.h"

#include <functional>

#include "Managers/Singletons/SettingsManager.h"
#include "Render/RenderGraph/RenderGraph.h"

namespace rg::runtime {

namespace {
class DefaultRenderGraphSettingsService final : public IRenderGraphSettingsService {
public:
    DefaultRenderGraphSettingsService() {
        m_getUseAsyncCompute = SettingsManager::GetInstance().getSettingGetter<bool>("useAsyncCompute");
        m_getAutoAliasMode = SettingsManager::GetInstance().getSettingGetter<AutoAliasMode>("autoAliasMode");
        m_getAutoAliasPackingStrategy = SettingsManager::GetInstance().getSettingGetter<AutoAliasPackingStrategy>("autoAliasPackingStrategy");
        m_getAutoAliasLogExclusionReasons = SettingsManager::GetInstance().getSettingGetter<bool>("autoAliasLogExclusionReasons");
        m_getAutoAliasPoolRetireIdleFrames = SettingsManager::GetInstance().getSettingGetter<uint32_t>("autoAliasPoolRetireIdleFrames");
        m_getAutoAliasPoolGrowthHeadroom = SettingsManager::GetInstance().getSettingGetter<float>("autoAliasPoolGrowthHeadroom");
    }

    bool GetUseAsyncCompute() const override {
        return m_getUseAsyncCompute ? m_getUseAsyncCompute() : false;
    }

    uint8_t GetAutoAliasMode() const override {
        return m_getAutoAliasMode ? static_cast<uint8_t>(m_getAutoAliasMode()) : static_cast<uint8_t>(AutoAliasMode::Off);
    }

    uint8_t GetAutoAliasPackingStrategy() const override {
        return m_getAutoAliasPackingStrategy
            ? static_cast<uint8_t>(m_getAutoAliasPackingStrategy())
            : static_cast<uint8_t>(AutoAliasPackingStrategy::GreedySweepLine);
    }

    bool GetAutoAliasLogExclusionReasons() const override {
        return m_getAutoAliasLogExclusionReasons ? m_getAutoAliasLogExclusionReasons() : false;
    }

    uint32_t GetAutoAliasPoolRetireIdleFrames() const override {
        return m_getAutoAliasPoolRetireIdleFrames ? m_getAutoAliasPoolRetireIdleFrames() : 120u;
    }

    float GetAutoAliasPoolGrowthHeadroom() const override {
        return m_getAutoAliasPoolGrowthHeadroom ? m_getAutoAliasPoolGrowthHeadroom() : 1.5f;
    }

private:
    std::function<bool()> m_getUseAsyncCompute;
    std::function<AutoAliasMode()> m_getAutoAliasMode;
    std::function<AutoAliasPackingStrategy()> m_getAutoAliasPackingStrategy;
    std::function<bool()> m_getAutoAliasLogExclusionReasons;
    std::function<uint32_t()> m_getAutoAliasPoolRetireIdleFrames;
    std::function<float()> m_getAutoAliasPoolGrowthHeadroom;
};
}

std::shared_ptr<IRenderGraphSettingsService> CreateDefaultRenderGraphSettingsService() {
    return std::make_shared<DefaultRenderGraphSettingsService>();
}

}
