#pragma once

#include <DirectXMath.h>
#include <flecs.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <array>

#include "BufferView.h"
#include "buffers.h"

class MeshInstance;
class DynamicGloballyIndexedResource;
class Texture;
class Mesh;

namespace Components {
	struct Position {
		Position() : pos(DirectX::XMVectorZero()) {}
		Position(const DirectX::XMVECTOR& position) : pos(position) {}
		Position(float x, float y, float z) : pos(DirectX::XMVectorSet(x, y, z, 0.0f)) {}
		Position(float x, float y, float z, float w) : pos(DirectX::XMVectorSet(x, y, z, w)) {}
		Position(const DirectX::XMFLOAT3& position) : pos(DirectX::XMVectorSet(position.x, position.y, position.z, 0.0f)) {}
		DirectX::XMVECTOR pos;
	};
	struct Rotation {
		Rotation() : rot(DirectX::XMQuaternionIdentity()) {}
		Rotation(const DirectX::XMVECTOR& rotation) : rot(rotation) {}
		Rotation(float roll, float pitch, float yaw) : rot(DirectX::XMQuaternionRotationRollPitchYaw(roll, pitch, yaw)) {}
		Rotation(float x, float y, float z, float w) : rot(DirectX::XMVectorSet(x, y, z, w)) {}
		Rotation(const DirectX::XMFLOAT4& rotation) : rot(DirectX::XMLoadFloat4(&rotation)) {}
		Rotation(const DirectX::XMFLOAT3& rotation) : rot(DirectX::XMQuaternionRotationRollPitchYaw(rotation.x, rotation.y, rotation.z)) {}
		DirectX::XMVECTOR rot;
	};
	struct Scale {
		Scale() : scale(DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f)) {}
		Scale(DirectX::XMVECTOR scale) : scale(scale) {}
		Scale(float x, float y, float z) : scale(DirectX::XMVectorSet(x, y, z, 0.0f)) {}
		Scale(DirectX::XMFLOAT3 scale) : scale(DirectX::XMVectorSet(scale.x, scale.y, scale.z, 0.0f)) {}
		DirectX::XMVECTOR scale;
	};
	struct Transform {
		Transform() : pos(DirectX::XMVectorZero()), rot(DirectX::XMQuaternionIdentity()), scale(DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f)) {}
		Transform(const Position& position, const Rotation& rotation, const Scale& scale)
			: pos(position.pos), rot(rotation.rot), scale(scale.scale) {
		}
		Position pos;
		Rotation rot;
		Scale scale;
	};
	struct Matrix {
		Matrix() : matrix(DirectX::XMMatrixIdentity()) {}
		Matrix(const DirectX::XMMATRIX& matrix) : matrix(matrix) {}
		Matrix(const DirectX::XMFLOAT4X4& matrix) : matrix(DirectX::XMLoadFloat4x4(&matrix)) {}
		DirectX::XMMATRIX matrix;
	};

	struct ActiveScene {}; // Represents the current scene
	struct GameScene { flecs::entity pipeline; }; // Represents the scene for the game
	struct SceneRoot {}; // Parent for all entities unique to the scene
	struct RenderableObject {
		RenderableObject() = default;
		RenderableObject(PerObjectCB cb) : perObjectCB(cb) {}
		PerObjectCB perObjectCB;
	}; // Represents an object that can be rendered

	enum LightType {
		Point = 0,
		Spot = 1,
		Directional = 2
	};
	struct Light {
		Light() = default;
		Light(LightType type, DirectX::XMFLOAT3 color, DirectX::XMFLOAT3 attenuation, float range)
			: type(type), color(color), attenuation(attenuation), range(range) {
		}
		LightType type;
		DirectX::XMFLOAT3 color;
		DirectX::XMFLOAT3 attenuation;
		float range;
	}; // Represents a light source

	struct ProjectionMatrix {
		ProjectionMatrix() : matrix(DirectX::XMMatrixIdentity()) {}
		ProjectionMatrix(const DirectX::XMMATRIX& matrix) : matrix(matrix) {}
		ProjectionMatrix(const DirectX::XMFLOAT4X4& matrix) : matrix(DirectX::XMLoadFloat4x4(&matrix)) {}
		DirectX::XMMATRIX matrix;
	}; // Represents a projection matrix

	struct FrustrumPlanes {
		FrustrumPlanes() = default;
		FrustrumPlanes(std::vector<std::array<ClippingPlane, 6>> frustumPlanes)
			: m_frustumPlanes(std::move(frustumPlanes)) {
		}
		std::vector<std::array<ClippingPlane, 6>> m_frustumPlanes;
	}; // A set of frustrum clipping planes

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

	struct Skinned {};
	struct OpaqueSkinned {};
	struct AlphaTestSkinned {};
	struct BlendSkinned {};

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
}; // namespace Components