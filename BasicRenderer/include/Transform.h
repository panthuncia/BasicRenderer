#pragma once
#include <DirectXMath.h>

using namespace DirectX;

class Transform {
public:
    XMFLOAT3 pos;
    XMVECTOR rot;
    XMFLOAT3 scale;
    bool isDirty;
    XMFLOAT4X4 modelMatrix;

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

    XMFLOAT3 getGlobalPosition() const;
};