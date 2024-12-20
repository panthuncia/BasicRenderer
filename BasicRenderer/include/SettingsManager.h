#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <typeinfo>
#include <type_traits>
#include <stdexcept>

#include "Setting.h"

class SettingsManager {
public:
    static SettingsManager& GetInstance();

    // Registers a setting with the given name and initial value
    template<typename T>
    void registerSetting(const std::string& name, T initialValue) {
        settings[name] = std::make_unique<Setting<T>>(initialValue);
    }

    // Returns a setter callable for the specified setting by name
    template<typename T>
    std::function<void(T)> getSettingSetter(const std::string& name) {
        auto& setting = getSettingByName(name);

        // Type check: Ensure that the requested type matches the stored type
        if (setting.getType() != typeid(T)) {
            throw std::runtime_error("Type mismatch for setting: " + name);
        }

        auto setter = setting.getSetter();
        return [setter](T newValue) {
            setter(static_cast<void*>(&newValue));
        };
    }

    // Returns a getter callable for the specified setting by name
    template<typename T>
    std::function<T(void)> getSettingGetter(const std::string& name) {
        auto& setting = getSettingByName(name);


        // Cast and return the getter
        auto getter = setting.getGetter();
        return [getter]() -> T {
            return static_cast<T>(*static_cast<typename std::remove_reference<T>::type*>(getter()));
        };
    }

    // Adds an observer to a specific setting
    template<typename T>
    void addObserver(const std::string& name, const std::function<void(const T&)>& observer) {
        auto& setting = getSettingByName(name);

        if (setting.getType() != typeid(T)) {
            throw std::runtime_error("Type mismatch for setting: " + name);
        }

        // Cast the setting to Setting<T> and add the observer
        auto typedSetting = dynamic_cast<Setting<T>*>(&setting);
        if (typedSetting) {
            typedSetting->addObserver(observer);
        }
        else {
            throw std::runtime_error("Failed to add observer for setting: " + name);
        }
    }

private:
    std::unordered_map<std::string, std::unique_ptr<ISetting>> settings;
   
    SettingsManager() = default;
    // Helper function to retrieve a setting by name
    ISetting& getSettingByName(const std::string& name) {
        if (settings.find(name) == settings.end()) {
            throw std::runtime_error("Setting not found: " + name);
        }
        return *settings[name];
    }
};

inline SettingsManager& SettingsManager::GetInstance() {
    static SettingsManager instance;
    return instance;
}