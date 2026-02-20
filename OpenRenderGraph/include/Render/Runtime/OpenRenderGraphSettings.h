#pragma once

#include <algorithm>
#include <cstdint>
#include <mutex>

namespace rg::runtime {

struct OpenRenderGraphSettings {
    uint8_t numFramesInFlight = 3;
    bool collectPipelineStatistics = false;

    bool useAsyncCompute = true;
    uint8_t autoAliasMode = 2;
    uint8_t autoAliasPackingStrategy = 0;
    bool autoAliasLogExclusionReasons = false;
    uint32_t autoAliasPoolRetireIdleFrames = 120u;
    float autoAliasPoolGrowthHeadroom = 1.5f;
};

namespace detail {
struct OpenRenderGraphSettingsState {
    std::mutex mutex;
    OpenRenderGraphSettings settings;
};

inline OpenRenderGraphSettingsState& GetOpenRenderGraphSettingsState() {
    static OpenRenderGraphSettingsState state;
    return state;
}
}

inline void SetOpenRenderGraphSettings(const OpenRenderGraphSettings& settings) {
    auto& state = detail::GetOpenRenderGraphSettingsState();
    std::scoped_lock lock(state.mutex);
    state.settings = settings;
    state.settings.numFramesInFlight = (std::max)(uint8_t{ 1 }, state.settings.numFramesInFlight);
    state.settings.autoAliasPoolRetireIdleFrames = (std::max)(1u, state.settings.autoAliasPoolRetireIdleFrames);
    state.settings.autoAliasPoolGrowthHeadroom = (std::max)(1.0f, state.settings.autoAliasPoolGrowthHeadroom);
}

inline OpenRenderGraphSettings GetOpenRenderGraphSettings() {
    auto& state = detail::GetOpenRenderGraphSettingsState();
    std::scoped_lock lock(state.mutex);
    return state.settings;
}

}
