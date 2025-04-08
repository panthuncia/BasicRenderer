#include "MathUtils.h"

using namespace DirectX;

void ApplyMovement(Components::Position& pos, const Components::Rotation& rot, const MovementState& movement, float deltaTime){
    // Compute movement directions based on rotation.
    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, -1, 0), rot.rot);
    XMVECTOR right   = XMVector3Rotate(XMVectorSet(1, 0, 0, 0), rot.rot);
    XMVECTOR up      = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), rot.rot);

    // Compute displacement.
    XMVECTOR displacement = forward * (movement.forwardMagnitude - movement.backwardMagnitude) * deltaTime +
        right   * (movement.rightMagnitude - movement.leftMagnitude) * deltaTime +
        up      * (movement.upMagnitude - movement.downMagnitude) * deltaTime;

    // Update position.
    pos.pos = XMVectorAdd(pos.pos, displacement);
}

void RotatePitchYaw(Components::Rotation& rot, float pitch, float yaw) {
    XMVECTOR yawQuat = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw);
    XMVECTOR pitchQuat = XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), pitch);
    rot.rot = XMQuaternionMultiply(rot.rot, yawQuat); // Multiplication order is important. If this is reversed, you get a solidworks-style camera
    rot.rot = XMQuaternionMultiply(pitchQuat, rot.rot);
}

XMVECTOR GetForwardFromMatrix(const DirectX::XMMATRIX& matrix) {
    return -XMVector3Normalize(matrix.r[2]);
}
XMVECTOR GetUpFromMatrix(const DirectX::XMMATRIX& matrix) {
    return -XMVector3Normalize(matrix.r[1]);
}