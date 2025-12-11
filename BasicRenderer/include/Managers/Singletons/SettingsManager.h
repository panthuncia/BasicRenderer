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

    class Subscription {
    public:
        Subscription() = default;
		Subscription(Subscription&& o) noexcept // Move constructor
            : _unsubscribe(std::move(o._unsubscribe))
        {
            o._unsubscribe = nullptr;
        }
        Subscription& operator=(Subscription&& o) noexcept {
            if (this != &o) {
                cancel();
                _unsubscribe = std::move(o._unsubscribe);
                o._unsubscribe = nullptr;
            }
            return *this;
        }
        ~Subscription() { cancel(); }

        void cancel() {
            if (_unsubscribe) {
                _unsubscribe();
                _unsubscribe = nullptr;
            }
        }

        // no copies
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;

    private:
        explicit Subscription(std::function<void()> unsub)
            : _unsubscribe(std::move(unsub))
        {
        }

        std::function<void()> _unsubscribe;


        friend class SettingsManager;
    };

    template<typename T>
    Subscription addObserver(const std::string& name,
        std::function<void(const T&)> obs)
    {
        auto& s = dynamic_cast<Setting<T>&>(getSettingByName(name));
        size_t id = s.addObserver(std::move(obs));
        // capture everything we need to remove it later
        return Subscription([this, name, id]() {
            removeObserver<T>(name, id);
            });
    }

    template<typename T>
    void removeObserver(const std::string& name, size_t id) {
        auto& s = dynamic_cast<Setting<T>&>(getSettingByName(name));
        s.removeObserver(id);
    }

    // Registers a dependency where 'controlledName' is updated based on 'controllerName' changing.
    // The resolver function takes (newControllerValue, currentControlledValue) and returns the newControlledValue.
    template<typename TController, typename TControlled>
    void registerDependency(const std::string& controllerName, const std::string& controlledName,
        std::function<TControlled(const TController&, const TControlled&)> resolver)
    {
        auto sub = addObserver<TController>(controllerName,
            [this, controlledName, resolver](const TController& controllerVal) {
                auto getter = getSettingGetter<TControlled>(controlledName);
                TControlled currentVal = getter();
                TControlled newVal = resolver(controllerVal, currentVal);
                if (newVal != currentVal) {
                    getSettingSetter<TControlled>(controlledName)(newVal);
                }
            });
        m_dependencySubscriptions.push_back(std::move(sub));
    }

private:
    std::unordered_map<std::string, std::unique_ptr<ISetting>> settings;
    std::vector<Subscription> m_dependencySubscriptions;

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