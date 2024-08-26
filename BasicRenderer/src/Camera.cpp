#include "camera.h"

using namespace DirectX;
Camera::Camera(std::string name, XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar) : lookAt(lookAt), up(up), fieldOfView(fov), aspectRatio(aspect), zNear(zNear), zFar(zFar), SceneNode(name) {
    // Initialize matrices
    viewMatrix = XMMatrixIdentity();
    viewMatrixInverse = XMMatrixIdentity();
    projectionMatrix = XMMatrixIdentity();
    viewProjectionMatrix = XMMatrixIdentity();
    viewProjectionMatrixInverse = XMMatrixIdentity();

    // Setup perspective projection matrix
    projectionMatrix = XMMatrixPerspectiveFovLH(fieldOfView, aspectRatio, zNear, zFar);
}

void Camera::onUpdate() {
    viewMatrix = XMMatrixInverse(nullptr, this->transform.modelMatrix);
    UpdateViewProjectionMatrix();
}