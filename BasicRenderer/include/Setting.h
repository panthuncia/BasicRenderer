#pragma once

#include <typeinfo>
#include <functional>

#include "Interfaces/ISetting.h"

template<typename T>
class Setting : public ISetting {
public:
	Setting(T initialValue) : value(initialValue) {}

	// Return the type information for this setting
	const std::type_info& getType() const override {
		return typeid(T);
	}

	// Setter callable takes a void pointer to the value and casts it
	std::function<void(void*)> getSetter() override {
		return [this](void* newValue) {
			value = *static_cast<T*>(newValue);
		};
	}

	// Getter callable returns a void pointer to the current value
	std::function<void* (void)> getGetter() override {
		return [this]() -> void* {
			return &value;
		};
	}

private:
	T value;
};