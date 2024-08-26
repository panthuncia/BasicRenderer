#pragma once

#include "Windowsx.h"
#include "InputContext.h"

class InputManager {
public:
    void SetInputContext(InputContext* context) {
        currentContext = context;
    }

    void ProcessInput(UINT message, WPARAM wParam, LPARAM lParam) {
        std::cout << "In process input" << std::endl;
        InputData inputData = {};
        inputData.mouseX = GET_X_LPARAM(lParam);
        inputData.mouseY = GET_Y_LPARAM(lParam);
        inputData.scrollDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (currentContext) {
            std::cout << "Has current context" << std::endl;
            currentContext->ProcessInput(message, wParam, lParam, inputData);
        }
    }

    void RegisterAction(InputAction action, std::function<void(float magnitude, const InputData& inputData)> handler) {
        if (currentContext) {
            currentContext->SetActionHandler(action, handler);
        }
    }

    InputContext* GetCurrentContext() {
        return currentContext;
    }

private:
    InputContext* currentContext = nullptr;
};