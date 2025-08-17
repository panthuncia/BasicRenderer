#include "Utilities/MathUtils.h"

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

    // Rearranged equation: quadratic * d^2 + linear * d + (constant - intensity/threshold) = 0
    float a = quadratic;
    float b = linear;
    float c = constant - (intensity / threshold);

    float d = 0.0;
    // If quadratic term is significant, solve via quadratic formula
    if (abs(a) > 1e-6) {
        float discriminant = static_cast<float>(b * b - 4.0 * a * c);
        // In case of a negative discriminant, there is no real solution. Return 0.
        if (discriminant < 0.0)
            d = 0.0;
        else
            d = (-b + sqrt(discriminant)) / (2.0f * a);
    } 
    else if (abs(b) > 1e-6) {  // Fall back to linear solution if a is negligible
        d = -c / b;
    }
    else {
        d = 0.0; // No attenuation factors; effective range is undefined.
    }

    return d;
}

BoundingSphere ComputeConeBoundingSphere(const XMVECTOR& origin, const XMVECTOR& direction, float height, float halfAngle) {
	float r = height * std::tan(halfAngle);

	float t = sqrt(height * height + r * r);

	XMVECTOR center = origin + DirectX::XMVector4Normalize(direction) * height / 2;

	BoundingSphere sphere;
	XMStoreFloat4(&sphere.sphere, center);
	sphere.sphere.w = t;

	return sphere;
}

unsigned int GetNextPowerOfTwo(unsigned int value) {
	if (value == 0) return 1; // Handle zero case
	value -= 1; // Decrement to handle exact powers of two
	value |= value >> 1;
	value |= value >> 2;
	value |= value >> 4;
	value |= value >> 8;
	value |= value >> 16;
	return value + 1; // Increment to get the next power of two
}

DirectX::XMFLOAT2 hammersley(uint i, float numSamples)
{
    uint bits = i;
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
    return {i / numSamples, bits / exp2(32.f)};
}

float Halton(uint32_t i, uint32_t b)
{
    float f = 1.0f;
    float r = 0.0f;

    while (i > 0)
    {
        f /= static_cast<float>(b);
        r = r + f * static_cast<float>(i % b);
        i = static_cast<uint32_t>(floorf(static_cast<float>(i) / static_cast<float>(b)));
    }

    return r;
}