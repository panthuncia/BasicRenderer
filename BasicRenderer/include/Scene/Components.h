#pragma once

#include <DirectXMath.h>
#include <flecs.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <array>
#include <optional>
#include <string>

#include "Resources/Buffers/BufferView.h"
#include "ShaderBuffers.h"

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
		Light(LightType type, DirectX::XMFLOAT3 color, DirectX::XMFLOAT3 attenuation, float range, LightInfo info)
			: type(type), color(color), attenuation(attenuation), range(range), lightInfo(info) {
		}
		LightType type;
		DirectX::XMFLOAT3 color;
		DirectX::XMFLOAT3 attenuation;
		float range;
		LightInfo lightInfo;
	}; // Represents a light source

	struct Camera {
		Camera() = default;
		Camera(float aspect, float fov, float zNear, float zFar, const CameraInfo& info) : info(info) {}
		float aspect;
		float fov;
		float zNear;
		float zFar;
		CameraInfo info;
	};

	struct ProjectionMatrix {
		ProjectionMatrix() : matrix(DirectX::XMMatrixIdentity()) {}
		ProjectionMatrix(const DirectX::XMMATRIX& matrix) : matrix(matrix) {}
		ProjectionMatrix(const DirectX::XMFLOAT4X4& matrix) : matrix(DirectX::XMLoadFloat4x4(&matrix)) {}
		DirectX::XMMATRIX matrix;
	}; // Represents a projection matrix

	struct FrustrumPlanes {
		FrustrumPlanes() = default;
		FrustrumPlanes(std::vector<std::array<ClippingPlane, 6>> frustumPlanes)
			: frustumPlanes(std::move(frustumPlanes)) {
		}
		std::vector<std::array<ClippingPlane, 6>> frustumPlanes;
	}; // A set of frustrum clipping planes

	struct IndirectDrawInfo {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
	};

	struct ObjectDrawInfo {
		std::optional<IndirectDrawInfo> opaque;
		std::optional<IndirectDrawInfo> alphaTest;
		std::optional<IndirectDrawInfo> blend;
		std::shared_ptr<BufferView> perObjectCBView;
		uint64_t perObjectCBIndex;
		std::shared_ptr<BufferView> normalMatrixView;
		uint64_t normalMatrixIndex;
	};

	struct IndirectCommandBuffers {
		std::shared_ptr<DynamicGloballyIndexedResource> opaqueIndirectCommandBuffer;
		std::shared_ptr<DynamicGloballyIndexedResource> alphaTestIndirectCommandBuffer;
		std::shared_ptr<DynamicGloballyIndexedResource> blendIndirectCommandBuffer;
		std::shared_ptr<DynamicGloballyIndexedResource> meshletFrustrumCullingIndirectCommandBuffer;
	};
	struct RenderView {
		uint64_t viewID;
		std::shared_ptr<BufferView> cameraBufferView;;
		uint64_t cameraBufferIndex;
		IndirectCommandBuffers indirectCommandBuffers;
		std::shared_ptr<DynamicGloballyIndexedResource> meshletBitfieldBuffer;
		std::shared_ptr<DynamicGloballyIndexedResource> meshInstanceBitfieldBuffer;
	};
	struct LightViewInfo {
		std::vector<RenderView> renderViews;
		std::shared_ptr<BufferView> lightBufferView;
		uint64_t lightBufferIndex;
		uint64_t viewInfoBufferIndex;
		Matrix projectionMatrix;
	};
	struct ShadowMap {
		ShadowMap() = default;
		ShadowMap(std::shared_ptr<Texture> shadowMap, std::shared_ptr<Texture> downsampledShadowMap)
			: shadowMap(shadowMap), downsampledShadowMap(downsampledShadowMap) {
		}
		std::shared_ptr<Texture> shadowMap;
		std::shared_ptr<Texture> downsampledShadowMap;
	};

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

	struct Skinned {};
	struct OpaqueSkinned {};
	struct AlphaTestSkinned {};
	struct BlendSkinned {};
	struct SkeletonRoot {}; // Tags the root of a skeleton hierarchy

	struct Active {}; // Represents an active entity in the scene
	struct Animated {}; // Animated nodes are ticked separately
	struct SkipShadowPass {}; // Skip the shadow pass for this entity

	struct Name {
		Name() = default;
		Name(std::string name) : name(std::move(name)) {}
		Name(const char* name) : name(name) {}
		std::string name;
	}; // The name of the entity

	struct AnimationName {
		AnimationName() = default;
		AnimationName(std::string name) : name(std::move(name)) {}
		std::string name;
	}; // The name a bone is referenced by in animations that affect it

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