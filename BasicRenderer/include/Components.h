#pragma once

#include <DirectXMath.h>
#include <flecs.h>
#include <memory>

#include "BufferView.h"

class MeshInstance;

using namespace DirectX;

namespace Components {
	struct Position {
		Position() : pos(XMVectorZero()) {}
		Position(const XMVECTOR& position) : pos(position) {}
		Position(float x, float y, float z) : pos(XMVectorSet(x, y, z, 0.0f)) {}
		Position(float x, float y, float z, float w) : pos(XMVectorSet(x, y, z, w)) {}
		Position(const XMFLOAT3& position) : pos(XMVectorSet(position.x, position.y, position.z, 0.0f)) {}
		XMVECTOR pos;
	};
	struct Rotation {
		Rotation() : rot(XMQuaternionIdentity()) {}
		Rotation(const XMVECTOR& rotation) : rot(rotation) {}
		Rotation(float roll, float pitch, float yaw) : rot(XMQuaternionRotationRollPitchYaw(roll, pitch, yaw)) {}
		Rotation(float x, float y, float z, float w) : rot(XMVectorSet(x, y, z, w)) {}
		Rotation(const XMFLOAT4& rotation) : rot(XMLoadFloat4(&rotation)) {}
		Rotation(const XMFLOAT3& rotation) : rot(XMQuaternionRotationRollPitchYaw(rotation.x, rotation.y, rotation.z)) {}
		XMVECTOR rot;
	};
	struct Scale {
		Scale() : scale(XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f)) {}
		Scale(XMVECTOR scale) : scale(scale) {}
		Scale(float x, float y, float z) : scale(XMVectorSet(x, y, z, 0.0f)) {}
		Scale(XMFLOAT3 scale) : scale(XMVectorSet(scale.x, scale.y, scale.z, 0.0f)) {}
		XMVECTOR scale;
	};
	struct Transform {
		Transform() : pos(XMVectorZero()), rot(XMQuaternionIdentity()), scale(XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f)) {}
		Transform(const Position& position, const Rotation& rotation, const Scale& scale)
			: pos(position.pos), rot(rotation.rot), scale(scale.scale) {
		}
		Position pos;
		Rotation rot;
		Scale scale;
	};
	struct Matrix {
		Matrix() : matrix(XMMatrixIdentity()) {}
		Matrix(const XMMATRIX& matrix) : matrix(matrix) {}
		Matrix(const XMFLOAT4X4& matrix) : matrix(XMLoadFloat4x4(&matrix)) {}
		XMMATRIX matrix;
	};

	struct ActiveScene {}; // Represents the current scene
	struct GameScene { flecs::entity pipeline; }; // Represents the scene for the game
	struct SceneRoot {}; // Parent for all entities unique to the scene
	struct RenderableObject {}; // Represents an object that can be rendered
	struct Light {}; // Represents a light source
	struct Camera {}; // Represents a camera
	struct SceneNode {}; // Represents a generic node in the scene graph

	struct RenderData {
		BufferView perObjectBufferView;
	};

	struct IndirectDrawData {
		std::vector<unsigned int> opaqueDrawSetIndices;
		std::vector<unsigned int> alphaTestDrawSetIndices;
		std::vector<unsigned int> blendDrawSetIndices;
		std::vector<BufferView> opaqueDrawSetCommandViews;
		std::vector<BufferView> alphaTestDrawSetCommandViews;
		std::vector<BufferView> blendDrawSetCommandViews;
	};

	struct OpaqueMeshInstances {
		OpaqueMeshInstances() = default;
		OpaqueMeshInstances(std::vector<std::unique_ptr<MeshInstance>> instances)
			: meshInstances(std::move(meshInstances)) {
		}
		std::vector<std::unique_ptr<MeshInstance>> meshInstances;
	};

	struct AlphaTestMeshInstances {
		AlphaTestMeshInstances() = default;
		AlphaTestMeshInstances(std::vector<std::unique_ptr<MeshInstance>> instances)
			: meshInstances(std::move(meshInstances)) {
		}
		std::vector<std::unique_ptr<MeshInstance>> meshInstances;
	};

	struct BlendMeshInstances {
		BlendMeshInstances() = default;
		BlendMeshInstances(std::vector<std::unique_ptr<MeshInstance>> instances)
			: meshInstances(std::move(meshInstances)) {
		}
		std::vector<std::unique_ptr<MeshInstance>> meshInstances;
	};
} // namespace Components