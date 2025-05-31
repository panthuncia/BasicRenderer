#pragma once
#include <DirectXMath.h>

#include "Scene/Components.h"
#include "Scene/MovementState.h"
#include "ShaderBuffers.h"

void ApplyMovement(Components::Position& pos, const Components::Rotation& rot, const MovementState& movement, float deltaTime);
void RotatePitchYaw(Components::Rotation& rot, float pitch, float yaw);
DirectX::XMVECTOR GetForwardFromMatrix(const DirectX::XMMATRIX& matrix);
DirectX::XMVECTOR GetUpFromMatrix(const DirectX::XMMATRIX& matrix);
float CalculateLightRadius(float intensity, float constant, float linear, float quadratic, float threshold = 0.3f);
BoundingSphere ComputeConeBoundingSphere(const DirectX::XMVECTOR& origin, const DirectX::XMVECTOR& direction, float height, float halfAngle);
unsigned int GetNextPowerOfTwo(unsigned int value);