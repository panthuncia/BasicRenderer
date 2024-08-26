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
    virtual void ProcessInput(UINT message, WPARAM wParam, LPARAM lParam, const InputData& inputData) = 0;

    void SetActionHandler(InputAction action, std::function<void(float magnitude, const InputData&)> handler) {
        actionHandlers[action] = handler;
    }

protected:
    void TriggerAction(InputAction action, float magnitude, InputData inputData) {

        if (actionHandlers.find(action) != actionHandlers.end()) {
            actionHandlers[action](magnitude, inputData);
        }
    }

private:
    std::unordered_map<InputAction, std::function<void(float magnitude, const InputData&)>> actionHandlers;
};

class WASDContext : public InputContext {
public:
    void ProcessInput(UINT message, WPARAM wParam, LPARAM lParam, const InputData& inputData) override {
        float magnitude = (message == WM_KEYDOWN) ? 1.0f : 0.0f;
        switch (message) {
        case WM_KEYDOWN: {
            WPARAM key = toupper(wParam);
            if (key == 'W') TriggerAction(InputAction::MoveForward, magnitude, inputData);
            else if (key == 'S') TriggerAction(InputAction::MoveBackward, magnitude, inputData);
            else if (key == 'A') TriggerAction(InputAction::MoveLeft, magnitude, inputData);
            else if (key == 'D') TriggerAction(InputAction::MoveRight, magnitude, inputData);
            else if (key == 16) TriggerAction(InputAction::MoveDown, magnitude, inputData);
            else if (key == ' ') TriggerAction(InputAction::MoveUp, magnitude, inputData);
            break;
        } case WM_KEYUP: {
            WPARAM key = toupper(wParam);
            if (key == 'W') TriggerAction(InputAction::MoveForward, 0.0f, inputData);
            else if (key == 'S') TriggerAction(InputAction::MoveBackward, 0.0f, inputData);
            else if (key == 'A') TriggerAction(InputAction::MoveLeft, 0.0f, inputData);
            else if (key == 'D') TriggerAction(InputAction::MoveRight, 0.0f, inputData);
            else if (key == 16) TriggerAction(InputAction::MoveDown, 0.0f, inputData);
            else if (key == ' ') TriggerAction(InputAction::MoveUp, 0.0f, inputData);
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
    void ProcessInput(UINT message, WPARAM wParam, LPARAM lParam, const InputData& inputData) override {
        switch (message) {
        case WM_MOUSEMOVE:
            TriggerAction(InputAction::RotateCamera, 1.0, inputData);
            break;
        case WM_MOUSEWHEEL:
            if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) {
                TriggerAction(InputAction::ZoomIn, 1.0, inputData);
            }
            else {
                TriggerAction(InputAction::ZoomOut, 1.0, inputData);
            }
            break;
            // Ignore WASD keys in this context
        default:
            break;
        }
    }
};