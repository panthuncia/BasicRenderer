#pragma once

#include <vector>
#include <string>
#include <DirectXMath.h>

#include "SceneNode.h"

class Camera : public SceneNode {
public:
	Camera(std::wstring name, XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar);

    void UpdateViewMatrix(XMFLOAT3 eye, XMFLOAT3 target, XMFLOAT3 upVec) {
        viewMatrix = XMMatrixLookAtRH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&upVec));
        viewMatrixInverse = XMMatrixInverse(nullptr, viewMatrix);
        UpdateViewProjectionMatrix();
    }

    void UpdateViewProjectionMatrix() {
        viewProjectionMatrix = XMMatrixMultiply(viewMatrix, projectionMatrix);
        viewProjectionMatrixInverse = XMMatrixInverse(nullptr, viewProjectionMatrix);
    }

    DirectX::XMMATRIX GetViewMatrix() const {
        return viewMatrix;
    }
    DirectX::XMMATRIX GetProjectionMatrix() const {
        return projectionMatrix;
    }

    float GetNear() const {
        return zNear;
    }

    float GetAspect() const {
        return aspectRatio;
    }

    float GetFOV() const {
        return fieldOfView;
    }


private:
protected:
    XMFLOAT3 lookAt;
    XMFLOAT3 up;
    XMMATRIX viewMatrix;
    XMMATRIX viewMatrixInverse;
    XMMATRIX projectionMatrix;
    XMMATRIX viewProjectionMatrix;
    XMMATRIX viewProjectionMatrixInverse;
    float fieldOfView;
    float aspectRatio;
    float zNear;
    float zFar;

	void OnUpdate() override;
};