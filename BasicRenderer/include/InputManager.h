#pragma once

#include "InputContext.h"

class InputManager {
public:
    void SetInputContext(InputContext* context) {
        currentContext = context;
    }

    void ProcessInput(UINT message, WPARAM wParam, LPARAM lParam) {
        std::cout << "In process input" << std::endl;
        if (currentContext) {
            std::cout << "Has current context" << std::endl;
            currentContext->ProcessInput(message, wParam, lParam);
        }
    }

    void RegisterAction(InputAction action, std::function<void()> handler) {
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