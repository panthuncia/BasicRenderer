#include "camera.h"
#include "utilities.h"
#include "ResourceManager.h"

using namespace DirectX;

Camera::Camera(std::wstring name, XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar) : lookAt(lookAt), up(up), fieldOfView(fov), aspectRatio(aspect), zNear(zNear), zFar(zFar), SceneNode(name) {
    m_cameraInfo.view = XMMatrixIdentity();

    m_cameraInfo.projection = XMMatrixPerspectiveFovRH(fieldOfView, aspectRatio, zNear, zFar);
	m_cameraInfo.viewProjection = m_cameraInfo.projection;

	auto planes = GetFrustumPlanesPerspective(aspectRatio, fieldOfView, zNear, zFar);
    m_cameraInfo.clippingPlanes[0] = planes[0];
    m_cameraInfo.clippingPlanes[1] = planes[1];
    m_cameraInfo.clippingPlanes[2] = planes[2];
    m_cameraInfo.clippingPlanes[3] = planes[3];
    m_cameraInfo.clippingPlanes[4] = planes[4];
    m_cameraInfo.clippingPlanes[5] = planes[5];
}

void Camera::OnUpdate() {
    auto inverseMatrix = XMMatrixInverse(nullptr, transform.modelMatrix);
    m_cameraInfo.view = RemoveScalingFromMatrix(inverseMatrix);
    UpdateViewProjectionMatrix();
	if (m_cameraBufferView) {
        auto pos = transform.getGlobalPosition();
		m_cameraInfo.positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };


		auto buffer = m_cameraBufferView->GetBuffer();
        buffer->UpdateView(m_cameraBufferView.get(), &m_cameraInfo);
	}
}