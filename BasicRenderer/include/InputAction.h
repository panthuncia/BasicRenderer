#pragma once

enum class InputAction {
    MoveForward,
    MoveBackward,
    MoveRight,
    MoveLeft,
    MoveUp,
    MoveDown,
    RotateCamera,
    ZoomIn,
    ZoomOut,
    Reset,
};

struct InputData {
    int mouseX;
    int mouseY;
    int mouseDeltaX;
    int mouseDeltaY;
    int scrollDelta;
};