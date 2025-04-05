#pragma once

#include <DirectXMath.h>
#include <flecs.h>
#include <memory>
#include <unordered_map>

#include "BufferView.h"
#include "buffers.h"

class MeshInstance;
class DynamicGloballyIndexedResource;
class Texture;
class Mesh;

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
	struct RenderableObject {
		PerObjectCB perObjectCB;
	}; // Represents an object that can be rendered

	enum LightType {
		Point = 0,
		Spot = 1,
		Directional = 2
	};
	struct Light {
		LightType type;
	}; // Represents a light source
	struct IndirectCommandBuffers {
		std::vector<std::shared_ptr<DynamicGloballyIndexedResource>> opaqueIndirectCommandBuffers;
		std::vector<std::shared_ptr<DynamicGloballyIndexedResource>> alphaTestIndirectCommandBuffers;
		std::vector<std::shared_ptr<DynamicGloballyIndexedResource>> blendIndirectCommandBuffers;
	};
	struct LightViewInfo {
		std::vector<std::shared_ptr<BufferView>> cameraBufferViews;
		IndirectCommandBuffers commandBuffers;
	};
	struct ShadowMap {
		std::shared_ptr<Texture> shadowMap;
	};
	struct Camera {}; // Represents a camera
	struct SceneNode {}; // Represents a generic node in the scene graph
	struct GlobalMeshLibrary {
		std::unordered_map<uint64_t, std::shared_ptr<Mesh>> meshes;
	};

	struct DrawStats {
		uint32_t numDrawsInScene = 0;
		uint32_t numOpaqueDraws = 0;
		uint32_t numAlphaTestDraws = 0;
		uint32_t numBlendDraws = 0;
	};

	struct RenderData {
		BufferView perObjectBufferView;
	};

	struct OpaqueIndirectDrawInfo {
		OpaqueIndirectDrawInfo() = default;
		OpaqueIndirectDrawInfo(std::vector<unsigned int> drawSetIndices, std::vector<BufferView> drawSetCommandViews)
			: drawSetIndices(std::move(drawSetIndices)), drawSetCommandViews(std::move(drawSetCommandViews)) {
		}
		std::vector<unsigned int> drawSetIndices;
		std::vector<BufferView> drawSetCommandViews;
	};

	struct AlphaTestIndirectDrawInfo {
		AlphaTestIndirectDrawInfo() = default;
		AlphaTestIndirectDrawInfo(std::vector<unsigned int> drawSetIndices, std::vector<BufferView> drawSetCommandViews)
			: drawSetIndices(std::move(drawSetIndices)), drawSetCommandViews(std::move(drawSetCommandViews)) {
		}
		std::vector<unsigned int> drawSetIndices;
		std::vector<BufferView> drawSetCommandViews;
	};

	struct BlendIndirectDrawInfo {
		BlendIndirectDrawInfo() = default;
		BlendIndirectDrawInfo(std::vector<unsigned int> drawSetIndices, std::vector<BufferView> drawSetCommandViews)
			: drawSetIndices(std::move(drawSetIndices)), drawSetCommandViews(std::move(drawSetCommandViews)) {
		}
		std::vector<unsigned int> drawSetIndices;
		std::vector<BufferView> drawSetCommandViews;
	};

	struct OpaqueMeshInstances {
		OpaqueMeshInstances() = default;
		OpaqueMeshInstances(std::vector<std::shared_ptr<MeshInstance>> instances)
			: meshInstances(std::move(meshInstances)) {
		}
		std::vector<std::shared_ptr<MeshInstance> > meshInstances;
	};

	struct AlphaTestMeshInstances {
		AlphaTestMeshInstances() = default;
		AlphaTestMeshInstances(std::vector<std::shared_ptr<MeshInstance>> instances)
			: meshInstances(std::move(meshInstances)) {
		}
		std::vector<std::shared_ptr<MeshInstance> > meshInstances;
	};

	struct BlendMeshInstances {
		BlendMeshInstances() = default;
		BlendMeshInstances(std::vector<std::shared_ptr<MeshInstance>> instances)
			: meshInstances(std::move(meshInstances)) {
		}
		std::vector<std::shared_ptr<MeshInstance> > meshInstances;
	};
} // namespace Components