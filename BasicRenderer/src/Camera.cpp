#include "camera.h"
#include "utilities.h"
using namespace DirectX;
Camera::Camera(std::wstring name, XMFLOAT3 lookAt, XMFLOAT3 up, float fov, float aspect, float zNear, float zFar) : lookAt(lookAt), up(up), fieldOfView(fov), aspectRatio(aspect), zNear(zNear), zFar(zFar), SceneNode(name) {
    // Initialize matrices
    viewMatrix = XMMatrixIdentity();
    viewMatrixInverse = XMMatrixIdentity();
    projectionMatrix = XMMatrixIdentity();
    viewProjectionMatrix = XMMatrixIdentity();
    viewProjectionMatrixInverse = XMMatrixIdentity();

    // Setup perspective projection matrix
    projectionMatrix = XMMatrixPerspectiveFovRH(fieldOfView, aspectRatio, zNear, zFar);
}

void Camera::OnUpdate() {
    viewMatrixInverse = transform.modelMatrix;
    auto inverseMatrix = XMMatrixInverse(nullptr, transform.modelMatrix);
    viewMatrix = RemoveScalingFromMatrix(inverseMatrix);
    UpdateViewProjectionMatrix();
}