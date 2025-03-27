#include "Transform.h"

#include "DefaultDirection.h"
DirectX::XMVECTOR defaultDirection = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

Transform::Transform(XMFLOAT3 pos, XMFLOAT3 rotEuler, XMFLOAT3 scale)
    : isDirty(false) {
	this->pos = DirectX::XMLoadFloat3(&pos);
	this->scale = DirectX::XMLoadFloat3(&scale);
    rot = XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3(&rotEuler));
    modelMatrix = XMMatrixIdentity();
}

Transform::Transform(const Transform& other)
    : pos(other.pos), scale(other.scale), isDirty(other.isDirty), rot(other.rot), modelMatrix(other.modelMatrix) {
}

XMMATRIX Transform::getLocalModelMatrix() {
    XMMATRIX matRotation = XMMatrixRotationQuaternion(rot);
    XMMATRIX matTranslation = XMMatrixTranslationFromVector(pos);
    XMMATRIX matScale = XMMatrixScalingFromVector(scale);
    return matScale * matRotation * matTranslation;
}

void Transform::computeLocalModelMatrix() {
    modelMatrix = getLocalModelMatrix();
    isDirty = false;
}

void Transform::computeModelMatrixFromParent(const XMMATRIX& parentGlobalModelMatrix) {
    modelMatrix = getLocalModelMatrix() * parentGlobalModelMatrix;
    isDirty = false;
}

void Transform::setLocalPosition(const XMVECTOR& newPosition) {
    pos = newPosition;
    isDirty = true;
}

void Transform::setLocalRotationFromEuler(const XMFLOAT3& rotEuler) {
    rot = XMQuaternionRotationRollPitchYawFromVector(XMVectorScale(XMLoadFloat3(&rotEuler), XM_PI / 180.0f));
    isDirty = true;
}

void Transform::rotateEuler(const XMFLOAT3& rotEuler) {
    XMVECTOR newQuat = XMQuaternionRotationRollPitchYawFromVector(XMVectorScale(XMLoadFloat3(&rotEuler), XM_PI / 180.0f));
    rot = XMQuaternionMultiply(rot, newQuat);
}

// Intended for cameras
void Transform::rotatePitchYaw(float pitch, float yaw) {
    XMVECTOR yawQuat = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw);
    XMVECTOR pitchQuat = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
    rot = XMQuaternionMultiply(rot, yawQuat); // Multiplication order is important. If this is reversed, you get a solidworks-style camera
    rot = XMQuaternionMultiply(pitchQuat, rot);
    isDirty = true;
}

void Transform::setLocalRotationFromQuaternion(const XMVECTOR& quaternion) {
    rot = quaternion;
    isDirty = true;
}

void Transform::setDirection(const XMFLOAT3& dir) {
    XMVECTOR targetDirection = XMVector3Normalize(XMLoadFloat3(&dir));
    float dotProduct = XMVectorGetX(XMVector3Dot(defaultDirection, targetDirection));

    if (dotProduct < -0.9999f) {
        XMVECTOR perpendicularAxis = XMVector3Cross(defaultDirection, XMVectorSet(1, 0, 0, 0));
        if (XMVector3Length(perpendicularAxis).m128_f32[0] < 0.01f) {
            perpendicularAxis = XMVector3Cross(defaultDirection, XMVectorSet(0, 1, 0, 0));
        }
        perpendicularAxis = XMVector3Normalize(perpendicularAxis);
        rot = XMQuaternionRotationAxis(perpendicularAxis, XM_PI);
    }
    else if (dotProduct > 0.9999f) {
        rot = XMQuaternionIdentity();
    }
    else {
        XMVECTOR rotationAxis = XMVector3Normalize(XMVector3Cross(defaultDirection, targetDirection));
        float rotationAngle = acosf(dotProduct);
        rot = XMQuaternionRotationAxis(rotationAxis, rotationAngle);
    }
    isDirty = true;
}

void Transform::setLocalScale(const XMVECTOR& newScale) {
    scale = newScale;
    isDirty = true;
}

XMFLOAT3 Transform::getGlobalPosition() const {
    XMFLOAT4X4 modelFloats;
    XMStoreFloat4x4(&modelFloats, modelMatrix);
    return XMFLOAT3(modelFloats._41, modelFloats._42, modelFloats._43);
}

void Transform::applyMovement(const MovementState& movement, float deltaTime) {
    // Compute direction vectors based on current rotation
    XMVECTOR forwardDir = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f), rot);
    XMVECTOR rightDir = XMVector3Rotate(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rot);
    XMVECTOR upDir = XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rot);

    // Update position based on movement state and delta time
    XMVECTOR displacement = XMVectorZero();
    displacement += forwardDir * (movement.forwardMagnitude - movement.backwardMagnitude) * deltaTime;
    displacement += rightDir * (movement.rightMagnitude - movement.leftMagnitude) * deltaTime;
    displacement += upDir * (movement.upMagnitude - movement.downMagnitude) * deltaTime;


    pos += displacement;
    isDirty = true;
}

Transform Transform::copy() const {
    Transform transform;
    transform.setLocalPosition(pos);
    transform.setLocalRotationFromQuaternion(rot);
    transform.setLocalScale(scale);
    transform.isDirty = true;
    return transform;
}

XMVECTOR Transform::GetForward() const {
    return -XMVector3Normalize(modelMatrix.r[2]);
}
XMVECTOR Transform::GetUp() const {
    return -XMVector3Normalize(modelMatrix.r[1]);
}