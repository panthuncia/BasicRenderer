#pragma once

#include "Components.h"
#include "Transform.h"

void ApplyMovement(Components::Position& pos, const Components::Rotation& rot, const MovementState& movement, float deltaTime);
void RotatePitchYaw(Components::Rotation& rot, float pitch, float yaw);