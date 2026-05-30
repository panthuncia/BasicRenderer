#pragma once

#include "Managers/Singletons/SettingsManager.h"

inline constexpr const char* MaterialTextureStreamingSettingName = "enableMaterialTextureStreaming";

inline bool IsMaterialTextureStreamingEnabledSetting() {
    try {
        return SettingsManager::GetInstance().getSettingGetter<bool>(MaterialTextureStreamingSettingName)();
    }
    catch (...) {
        return true;
    }
}
