#pragma once

#include <Windows.h>
#include <functional>
#include <unordered_map>
#include <iostream>
#include <spdlog/spdlog.h>
#include <cctype>

#include "InputAction.h"

enum class InputMode {
    wasd,
    orbital
};

class InputManager; // Forward declaration

class InputContext {
public:
    virtual void ProcessInput(UINT message, WPARAM wParam, LPARAM lParam) = 0;

    void SetActionHandler(InputAction action, std::function<void()> handler) {
        actionHandlers[action] = handler;
    }

protected:
    void TriggerAction(InputAction action) {

        if (actionHandlers.find(action) != actionHandlers.end()) {
            actionHandlers[action]();
        }
    }

private:
    std::unordered_map<InputAction, std::function<void()>> actionHandlers;
};

class WASDContext : public InputContext {
public:
    void ProcessInput(UINT message, WPARAM wParam, LPARAM lParam) override {
        switch (message) {
        case WM_KEYDOWN: {
            spdlog::info("Key pressed!");
            WPARAM key = toupper(wParam);
            if (key == 'W') TriggerAction(InputAction::MoveForward);
            else if (key == 'S') TriggerAction(InputAction::MoveBackward);
            else if (key == 'A') TriggerAction(InputAction::MoveLeft);
            else if (key == 'D') TriggerAction(InputAction::MoveRight);
            break;
        }
            // Ignore mouse scroll in this context
        default:
            break;
        }
    }
};

class OrbitalCameraContext : public InputContext {
public:
    void ProcessInput(UINT message, WPARAM wParam, LPARAM lParam) override {
        switch (message) {
        case WM_MOUSEMOVE:
            TriggerAction(InputAction::RotateCamera);
            break;
        case WM_MOUSEWHEEL:
            if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) {
                TriggerAction(InputAction::ZoomIn);
            }
            else {
                TriggerAction(InputAction::ZoomOut);
            }
            break;
            // Ignore WASD keys in this context
        default:
            break;
        }
    }
};