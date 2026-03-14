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

#include "Materials/TechniqueDescriptor.h"
#include "ShaderBuffers.h"

class MeshInstance;
class DynamicGloballyIndexedResource;
class TextureAsset;
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

    struct ActiveScene {};
    struct GameScene { flecs::entity pipeline; };
    struct SceneRoot {};
    struct StableSceneID {
        uint64_t value = 0;
    };

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
    };

    struct Camera {
        Camera() = default;
        Camera(float aspect, float fov, float zNear, float zFar, const CameraInfo& info) : aspect(aspect), fov(fov), zNear(zNear), zFar(zFar), info(info) {}
        float aspect;
        float fov;
        float zNear;
        float zFar;
        DirectX::XMFLOAT2 jitterPixelSpace = {};
        DirectX::XMFLOAT2 jitterNDC = {};
        CameraInfo info;
    };

    struct PrimaryCamera {};

    struct ProjectionMatrix {
        ProjectionMatrix() : matrix(DirectX::XMMatrixIdentity()) {}
        ProjectionMatrix(const DirectX::XMMATRIX& matrix) : matrix(matrix) {}
        ProjectionMatrix(const DirectX::XMFLOAT4X4& matrix) : matrix(DirectX::XMLoadFloat4x4(&matrix)) {}
        DirectX::XMMATRIX matrix;
    };

    struct FrustumPlanes {
        FrustumPlanes() = default;
        FrustumPlanes(std::vector<std::array<ClippingPlane, 6>> frustumPlanes)
            : frustumPlanes(std::move(frustumPlanes)) {
        }
        std::vector<std::array<ClippingPlane, 6>> frustumPlanes;
    };

    struct IndirectCommandBuffers {
        std::shared_ptr<DynamicGloballyIndexedResource> meshletCullingIndirectCommandBuffer;
        std::shared_ptr<DynamicGloballyIndexedResource> meshletCullingResetIndirectCommandBuffer;
    };

    struct SceneNode {};
    struct GlobalMeshLibrary {
        std::unordered_map<uint64_t, std::weak_ptr<Mesh>> meshes;
    };

    struct DrawStats {
        uint32_t numDrawsInScene = 0;
        std::unordered_map<MaterialCompileFlags, uint32_t> numDrawsPerTechnique;
    };

    struct Skinned {};
    struct SkeletonRoot {};

    struct Active {};
    struct Animated {};
    struct SkipShadowPass {};

    struct Name {
        Name() = default;
        Name(std::string name) : name(std::move(name)) {}
        Name(const char* name) : name(name) {}
        std::string name;
    };

    struct AnimationName {
        AnimationName() = default;
        AnimationName(std::string name) : name(std::move(name)) {}
        std::string name;
    };

    struct MeshInstances {
        MeshInstances() = default;
        MeshInstances(std::vector<std::shared_ptr<MeshInstance>> instances)
            : meshInstances(std::move(instances)) {
        }
        std::vector<std::shared_ptr<MeshInstance>> meshInstances;
    };

} // namespace Components
