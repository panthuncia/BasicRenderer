#pragma once
#include <DirectXMath.h>

using namespace DirectX;

struct MovementState {
    float forwardMagnitude = 0.0;
    float backwardMagnitude = 0.0;
    float rightMagnitude = 0.0;
    float leftMagnitude = 0.0;
    float upMagnitude = 0.0;
    float downMagnitude = 0.0;
};

class Transform {
public:
    XMFLOAT3 pos;
    XMVECTOR rot;
    XMFLOAT3 scale;
    bool isDirty;
    XMMATRIX modelMatrix;

    Transform(XMFLOAT3 pos = XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3 rotEuler = XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3 scale = XMFLOAT3(1.0f, 1.0f, 1.0f));
    Transform(const Transform& other);

    XMMATRIX getLocalModelMatrix();
    void computeLocalModelMatrix();
    void computeModelMatrixFromParent(const XMMATRIX& parentGlobalModelMatrix);

    void setLocalPosition(const XMFLOAT3& newPosition);
    void setLocalRotationFromEuler(const XMFLOAT3& rotEuler);
    void rotateEuler(const XMFLOAT3& rotEuler);
    void rotatePitchYaw(float pitch, float yaw);
    void setLocalRotationFromQuaternion(const XMVECTOR& quaternion);
    void setDirection(const XMFLOAT3& dir);
    void setLocalScale(const XMFLOAT3& newScale);
    void applyMovement(const MovementState& movement, float deltaTime);

    XMFLOAT3 getGlobalPosition() const;

    Transform copy() const;
};