#include "camera.h"
#include "utilities.h"
#include "ResourceManager.h"

using namespace DirectX;

Camera::Camera(std::wstring name, XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar) : lookAt(lookAt), up(up), fieldOfView(fov), aspectRatio(aspect), zNear(zNear), zFar(zFar), SceneNode(name) {
    viewMatrix = XMMatrixIdentity();
    viewMatrixInverse = XMMatrixIdentity();
    projectionMatrix = XMMatrixIdentity();
    viewProjectionMatrix = XMMatrixIdentity();
    viewProjectionMatrixInverse = XMMatrixIdentity();

    projectionMatrix = XMMatrixPerspectiveFovRH(fieldOfView, aspectRatio, zNear, zFar);

	m_clippingPlanes = GetFrustumPlanesPerspective(aspectRatio, fieldOfView, zNear, zFar);
}

void Camera::OnUpdate() {
    viewMatrixInverse = transform.modelMatrix;
    auto inverseMatrix = XMMatrixInverse(nullptr, transform.modelMatrix);
    viewMatrix = RemoveScalingFromMatrix(inverseMatrix);
    UpdateViewProjectionMatrix();
	if (m_cameraBufferView) {
        auto pUploadData = m_cameraBufferView->Map<CameraInfo>();
        auto pos = transform.getGlobalPosition();
        pUploadData->positionWorldSpace = { pos.x, pos.y, pos.z, 1.0 };
		pUploadData->view = viewMatrix;
		pUploadData->projection = projectionMatrix;
		pUploadData->viewProjection = viewProjectionMatrix;
		pUploadData->clippingPlanes[0] = m_clippingPlanes[0];
        pUploadData->clippingPlanes[1] = m_clippingPlanes[1];
        pUploadData->clippingPlanes[2] = m_clippingPlanes[2];
        pUploadData->clippingPlanes[3] = m_clippingPlanes[3];
        pUploadData->clippingPlanes[4] = m_clippingPlanes[4];
        pUploadData->clippingPlanes[5] = m_clippingPlanes[5];


		auto buffer = m_cameraBufferView->GetBuffer();
        buffer->MarkViewDirty(m_cameraBufferView.get());
        ResourceManager::GetInstance().QueueViewedDynamicBufferViewUpdate(m_cameraBufferView->GetBuffer());
	}
}