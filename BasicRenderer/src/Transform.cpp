#include "Transform.h"

Transform::Transform(XMFLOAT3 pos, XMFLOAT3 rotEuler, XMFLOAT3 scale)
    : pos(pos), scale(scale), isDirty(false) {
    rot = XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3(&rotEuler));
    modelMatrix = XMMatrixIdentity();
}

Transform::Transform(const Transform& other)
    : pos(other.pos), scale(other.scale), isDirty(other.isDirty), rot(other.rot), modelMatrix(other.modelMatrix) {
}

XMMATRIX Transform::getLocalModelMatrix() {
    XMMATRIX matRotation = XMMatrixRotationQuaternion(rot);
    XMMATRIX matTranslation = XMMatrixTranslationFromVector(XMLoadFloat3(&pos));
    XMMATRIX matScale = XMMatrixScalingFromVector(XMLoadFloat3(&scale));
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

void Transform::setLocalPosition(const XMFLOAT3& newPosition) {
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

void Transform::rotatePitchYaw(float pitch, float yaw) {
    XMVECTOR yawQuat = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw);
    XMVECTOR pitchQuat = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
    rot = XMQuaternionMultiply(yawQuat, rot);
    rot = XMQuaternionMultiply(rot, pitchQuat);
    isDirty = true;
}

void Transform::setLocalRotationFromQuaternion(const XMVECTOR& quaternion) {
    rot = quaternion;
    isDirty = true;
}

void Transform::setDirection(const XMFLOAT3& dir) {
    XMVECTOR targetDirection = XMVector3Normalize(XMLoadFloat3(&dir));
    XMVECTOR defaultDirection = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
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

void Transform::setLocalScale(const XMFLOAT3& newScale) {
    scale = newScale;
    isDirty = true;
}

XMFLOAT3 Transform::getGlobalPosition() const {
    XMFLOAT4X4 modelFloats;
    XMStoreFloat4x4(&modelFloats, modelMatrix);
    return XMFLOAT3(modelFloats._41, modelFloats._42, modelFloats._43);
}