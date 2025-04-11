#pragma once
#include <DirectXMath.h>

#include "Components.h"
#include "MovementState.h"

void ApplyMovement(Components::Position& pos, const Components::Rotation& rot, const MovementState& movement, float deltaTime);
void RotatePitchYaw(Components::Rotation& rot, float pitch, float yaw);
DirectX::XMVECTOR GetForwardFromMatrix(const DirectX::XMMATRIX& matrix);
DirectX::XMVECTOR GetUpFromMatrix(const DirectX::XMMATRIX& matrix);