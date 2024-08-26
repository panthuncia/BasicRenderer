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
};

struct InputData {
    int mouseX;
    int mouseY;
    int scrollDelta;
};