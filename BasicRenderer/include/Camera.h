#pragma once

#include <vector>
#include <string>
#include <DirectXMath.h>

#include "SceneNode.h"

class Camera : public SceneNode {
public:
	Camera(std::string name, XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar);

    // Function to update view matrix
    void UpdateViewMatrix(XMFLOAT3 eye, XMFLOAT3 target, XMFLOAT3 upVec) {
        viewMatrix = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&upVec));
        viewMatrixInverse = XMMatrixInverse(nullptr, viewMatrix);
        UpdateViewProjectionMatrix();
    }

    // Function to update view projection matrix
    void UpdateViewProjectionMatrix() {
        viewProjectionMatrix = XMMatrixMultiply(viewMatrix, projectionMatrix);
        viewProjectionMatrixInverse = XMMatrixInverse(nullptr, viewProjectionMatrix);
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

	void onUpdate() override;
};