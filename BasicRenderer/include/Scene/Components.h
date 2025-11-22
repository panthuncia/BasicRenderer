#pragma once

#include <DirectXMath.h>
#include <flecs.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <array>
#include <optional>
#include <string>
#include <unordered_set>

#include "Resources/Buffers/BufferView.h"
#include "ShaderBuffers.h"
#include "Materials/TechniqueDescriptor.h"

class MeshInstance;
class DynamicGloballyIndexedResource;
class Texture;
class PixelBuffer;
class Mesh;

namespace Components {
	struct Position {
		Position() : pos(DirectX::XMVectorZero()) {}
		Position(const DirectX::XMVECTOR& position) : pos(position) {}
		Position(double x, double y, double z) : pos(DirectX::XMVectorSet(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), 0.0f)) {}
		Position(double x, double y, double z, double w) : pos(DirectX::XMVectorSet(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), static_cast<float>(w))) {}
		Position(const DirectX::XMFLOAT3& position) : pos(DirectX::XMVectorSet(position.x, position.y, position.z, 0.0f)) {}
		DirectX::XMVECTOR pos;
	};
	struct Rotation {
		Rotation() : rot(DirectX::XMQuaternionIdentity()) {}
		Rotation(const DirectX::XMVECTOR& rotation) : rot(rotation) {}
		Rotation(double roll, double pitch, double yaw) : rot(DirectX::XMQuaternionRotationRollPitchYaw(static_cast<float>(roll), static_cast<float>(pitch), static_cast<float>(yaw))) {}
		Rotation(double x, double y, double z, double w) : rot(DirectX::XMVectorSet(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), static_cast<float>(w))) {}
		Rotation(const DirectX::XMFLOAT4& rotation) : rot(DirectX::XMLoadFloat4(&rotation)) {}
		Rotation(const DirectX::XMFLOAT3& rotation) : rot(DirectX::XMQuaternionRotationRollPitchYaw(rotation.x, rotation.y, rotation.z)) {}
		DirectX::XMVECTOR rot;
	};
	struct Scale {
		Scale() : scale(DirectX::XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f)) {}
		Scale(DirectX::XMVECTOR scale) : scale(scale) {}
		Scale(double x, double y, double z) : scale(DirectX::XMVectorSet(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), 0.0f)) {}
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
		Camera(float aspect, float fov, float zNear, float zFar, const CameraInfo& info) : aspect(aspect), fov(fov), zNear(zNear), zFar(zFar), info(info) {}
		float aspect;
		float fov;
		float zNear;
		float zFar;
		DirectX::XMFLOAT2 jitterPixelSpace = {}; // Jitter in pixel space for temporal anti-aliasing
		DirectX::XMFLOAT2 jitterNDC = {}; // Jitter in normalized device coordinates
		CameraInfo info;
	};

	struct PrimaryCamera {}; // Tag for the primary camera in the scene

	struct ProjectionMatrix {
		ProjectionMatrix() : matrix(DirectX::XMMatrixIdentity()) {}
		ProjectionMatrix(const DirectX::XMMATRIX& matrix) : matrix(matrix) {}
		ProjectionMatrix(const DirectX::XMFLOAT4X4& matrix) : matrix(DirectX::XMLoadFloat4x4(&matrix)) {}
		DirectX::XMMATRIX matrix;
	}; // Represents a projection matrix

	struct FrustumPlanes {
		FrustumPlanes() = default;
		FrustumPlanes(std::vector<std::array<ClippingPlane, 6>> frustumPlanes)
			: frustumPlanes(std::move(frustumPlanes)) {
		}
		std::vector<std::array<ClippingPlane, 6>> frustumPlanes;
	}; // A set of frustrum clipping planes

	struct IndirectDrawInfo {
		std::vector<unsigned int> indices;
		std::vector<std::shared_ptr<BufferView>> views;
		std::vector<MaterialCompileFlags> materialTechniques;
	};

	struct ObjectDrawInfo {
		IndirectDrawInfo drawInfo;
		std::shared_ptr<BufferView> perObjectCBView;
		uint32_t perObjectCBIndex;
		std::shared_ptr<BufferView> normalMatrixView;
		uint32_t normalMatrixIndex;
	};

	struct IndirectCommandBuffers {
		std::shared_ptr<DynamicGloballyIndexedResource> meshletCullingIndirectCommandBuffer;
		std::shared_ptr<DynamicGloballyIndexedResource> meshletCullingResetIndirectCommandBuffer;
	};
	struct DepthMap {
		DepthMap() = default;
		DepthMap(std::shared_ptr<PixelBuffer> depthMap, std::shared_ptr<PixelBuffer> linearDepthMap)
			: depthMap(depthMap), linearDepthMap(linearDepthMap) {
		}
		std::shared_ptr<PixelBuffer> depthMap;
		std::shared_ptr<PixelBuffer> linearDepthMap;
	};
	struct RenderViewRef {
		uint64_t viewID;
	};
	struct LightViewInfo {
		std::vector<uint64_t> viewIDs;
		std::shared_ptr<BufferView> lightBufferView;
		uint32_t lightBufferIndex;
		uint32_t viewInfoBufferIndex;
		Matrix projectionMatrix;
		std::shared_ptr<PixelBuffer> depthMap;
		std::shared_ptr<PixelBuffer> linearDepthMap;
		uint32_t depthResX;
		uint32_t depthResY;
	};

	struct SceneNode {}; // Represents a generic node in the scene graph
	struct GlobalMeshLibrary {
		std::unordered_map<uint64_t, std::weak_ptr<Mesh>> meshes;
	};

	struct DrawStats {
		uint32_t numDrawsInScene = 0;
		std::unordered_map<MaterialCompileFlags, uint32_t> numDrawsPerTechnique;
	};

	struct Skinned {};
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

	struct MeshInstances {
		MeshInstances() = default;
		MeshInstances(std::vector<std::shared_ptr<MeshInstance>> instances)
			: meshInstances(std::move(instances)) {
		}
		std::vector<std::shared_ptr<MeshInstance> > meshInstances;
	};
	struct PerPassMeshes {
		// Keyed by RenderPhase id hash
		std::unordered_map<uint64_t, std::vector<std::shared_ptr<MeshInstance>>> meshesByPass;
	};

}; // namespace Components