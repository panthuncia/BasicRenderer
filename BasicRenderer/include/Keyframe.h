#pragma once

#include <DirectXMath.h>

using namespace DirectX;

class Keyframe {
public:
    float time;
    XMFLOAT3 value; // For position and scale keyframes
    XMFLOAT4 rotation; // For rotation keyframes

    Keyframe(float t, const XMFLOAT3& v) : time(t), value(v) {}
    Keyframe(float t, const XMFLOAT4& r) : time(t), rotation(r) {}
};