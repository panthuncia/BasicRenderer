#pragma once

#include <vector>
#include <string>
#include <memory>
#include <DirectXMath.h>
#include <array>

#include "SceneNode.h"
#include "buffers.h"

class BufferView;

class Camera : public SceneNode {
public:
	Camera(std::wstring name, XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar);

    void UpdateViewMatrix(XMFLOAT3 eye, XMFLOAT3 target, XMFLOAT3 upVec) {
        m_cameraInfo.view = XMMatrixLookAtRH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMLoadFloat3(&upVec));
        //viewMatrixInverse = XMMatrixInverse(nullptr, viewMatrix);
        UpdateViewProjectionMatrix();
    }

    void UpdateViewProjectionMatrix() {
		m_cameraInfo.viewProjection = XMMatrixMultiply(m_cameraInfo.view, m_cameraInfo.projection);
        //viewProjectionMatrixInverse = XMMatrixInverse(nullptr, viewProjectionMatrix);
    }

    DirectX::XMMATRIX GetViewMatrix() const {
        return m_cameraInfo.view;
    }
    DirectX::XMMATRIX GetProjectionMatrix() const {
        return m_cameraInfo.projection;
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

	void SetCameraBufferView(std::shared_ptr<BufferView> view) {
		m_cameraBufferView = view;
	}

	std::shared_ptr<BufferView> GetCameraBufferView() {
		return m_cameraBufferView;
	}

private:
protected:
    XMFLOAT3 lookAt;
    XMFLOAT3 up;
    float fieldOfView;
    float aspectRatio;
    float zNear;
    float zFar;
	std::array<ClippingPlane, 6> m_clippingPlanes;
	CameraInfo m_cameraInfo;

	std::shared_ptr<BufferView> m_cameraBufferView = nullptr;

	void OnUpdate() override;
};