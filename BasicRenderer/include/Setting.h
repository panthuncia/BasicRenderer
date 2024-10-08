#pragma once

#include <typeinfo>
#include <functional>

#include "Interfaces/ISetting.h"

template<typename T>
class Setting : public ISetting {
public:
	Setting(T initialValue) : value(initialValue) {}

	// Return type information for this setting
	const std::type_info& getType() const override {
		return typeid(T);
	}

	// Setter callable takes a void pointer to the value and casts it
	std::function<void(void*)> getSetter() override {
		return [this](void* newValuePtr) {
			value = *static_cast<T*>(newValuePtr);
			notifyObservers();
		};
	}

	// Getter callable returns a void pointer to the current value
	std::function<void* (void)> getGetter() override {
		return [this]() -> void* {
			return &value;
		};
	}

	void addObserver(const std::function<void(const T&)>& observer) {
		observers.push_back(observer);
	}

	void removeObserver(const std::function<void(const T&)>& observer) {
		observers.erase(std::remove(observers.begin(), observers.end(), observer), observers.end());
	}

private:
	T value;
	std::vector<std::function<void(const T&)>> observers;

	void notifyObservers() {
		for (const auto& observer : observers) {
			observer(value);
		}
	}
};