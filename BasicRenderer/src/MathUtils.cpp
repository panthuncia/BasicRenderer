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

float CalculateLightRadius(float intensity, float constant, float linear, float quadratic, float threshold) {
	if (intensity <= 0.0f) return 0.0f;

	float c = constant - intensity / threshold;

	if (quadratic == 0.0f) {
		if (linear == 0.0f)
			return 0.0f;
		return std::max((intensity / threshold - constant) / linear, 0.0f);
	}

	float discriminant = linear * linear - 4.0f * quadratic * c;
	if (discriminant < 0.0f)
		return 0.0f;

	float d = (-linear + std::sqrt(discriminant)) / (2.0f * quadratic);
	return std::max(d, 0.0f);
}

BoundingSphere ComputeConeBoundingSphere(const XMVECTOR& origin, const XMVECTOR& direction, float height, float halfAngle) {
	float r = height * std::tan(halfAngle);

	float t = (height * height + r * r) / (2.0f * height);

	XMVECTOR center = origin + direction * t;

	BoundingSphere sphere;
	XMStoreFloat4(&sphere.center, center);
	sphere.radius = t;

	return sphere;
}