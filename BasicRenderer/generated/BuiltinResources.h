#pragma once

#include <string>

// GENERATED CODE, DO NOT EDIT

namespace Builtin {
namespace ActiveDrawSetIndices {
inline constexpr std::string_view AlphaTest = "Builtin::ActiveDrawSetIndices::AlphaTest";
inline constexpr std::string_view Blend = "Builtin::ActiveDrawSetIndices::Blend";
inline constexpr std::string_view Opaque = "Builtin::ActiveDrawSetIndices::Opaque";
}
inline constexpr std::string_view CameraBuffer = "Builtin::CameraBuffer";
namespace Color {
inline constexpr std::string_view HDRColorTarget = "Builtin::Color::HDRColorTarget";
inline constexpr std::string_view SRGBColorTarget = "Builtin::Color::SRGBColorTarget";
}
inline constexpr std::string_view DebugTexture = "Builtin::DebugTexture";
namespace Environment {
inline constexpr std::string_view CurrentCubemap = "Builtin::Environment::CurrentCubemap";
inline constexpr std::string_view InfoBuffer = "Builtin::Environment::InfoBuffer";
inline constexpr std::string_view PrefilteredCubemapsGroup = "Builtin::Environment::PrefilteredCubemapsGroup";
inline constexpr std::string_view WorkingCubemapGroup = "Builtin::Environment::WorkingCubemapGroup";
inline constexpr std::string_view WorkingHDRIGroup = "Builtin::Environment::WorkingHDRIGroup";
}
namespace GBuffer {
inline constexpr std::string_view Albedo = "Builtin::GBuffer::Albedo";
inline constexpr std::string_view Emissive = "Builtin::GBuffer::Emissive";
inline constexpr std::string_view MetallicRoughness = "Builtin::GBuffer::MetallicRoughness";
inline constexpr std::string_view MotionVectors = "Builtin::GBuffer::MotionVectors";
inline constexpr std::string_view Normals = "Builtin::GBuffer::Normals";
}
namespace GTAO {
inline constexpr std::string_view OutputAOTerm = "Builtin::GTAO::OutputAOTerm";
inline constexpr std::string_view WorkingAOTerm1 = "Builtin::GTAO::WorkingAOTerm1";
inline constexpr std::string_view WorkingAOTerm2 = "Builtin::GTAO::WorkingAOTerm2";
inline constexpr std::string_view WorkingDepths = "Builtin::GTAO::WorkingDepths";
inline constexpr std::string_view WorkingEdges = "Builtin::GTAO::WorkingEdges";
}
namespace IndirectCommandBuffers {
inline constexpr std::string_view Master = "Builtin::IndirectCommandBuffers::Master";
inline constexpr std::string_view MeshletCulling = "Builtin::IndirectCommandBuffers::MeshletCulling";
inline constexpr std::string_view Primary = "Builtin::IndirectCommandBuffers::Primary";
}
namespace Light {
inline constexpr std::string_view ActiveLightIndices = "Builtin::Light::ActiveLightIndices";
inline constexpr std::string_view BufferGroup = "Builtin::Light::BufferGroup";
inline constexpr std::string_view ClusterBuffer = "Builtin::Light::ClusterBuffer";
inline constexpr std::string_view DirectionalLightCascadeBuffer = "Builtin::Light::DirectionalLightCascadeBuffer";
inline constexpr std::string_view InfoBuffer = "Builtin::Light::InfoBuffer";
inline constexpr std::string_view PagesBuffer = "Builtin::Light::PagesBuffer";
inline constexpr std::string_view PagesCounter = "Builtin::Light::PagesCounter";
inline constexpr std::string_view PointLightCubemapBuffer = "Builtin::Light::PointLightCubemapBuffer";
inline constexpr std::string_view SpotLightMatrixBuffer = "Builtin::Light::SpotLightMatrixBuffer";
inline constexpr std::string_view ViewResourceGroup = "Builtin::Light::ViewResourceGroup";
}
inline constexpr std::string_view MeshInstanceMeshletCullingBitfieldGroup = "Builtin::MeshInstanceMeshletCullingBitfieldGroup";
inline constexpr std::string_view MeshInstanceOcclusionCullingBitfieldGroup = "Builtin::MeshInstanceOcclusionCullingBitfieldGroup";
namespace MeshResources {
inline constexpr std::string_view MeshletBounds = "Builtin::MeshResources::MeshletBounds";
inline constexpr std::string_view MeshletOffsets = "Builtin::MeshResources::MeshletOffsets";
inline constexpr std::string_view MeshletTriangles = "Builtin::MeshResources::MeshletTriangles";
inline constexpr std::string_view MeshletVertexIndices = "Builtin::MeshResources::MeshletVertexIndices";
}
inline constexpr std::string_view MeshletCullingBitfieldGroup = "Builtin::MeshletCullingBitfieldGroup";
inline constexpr std::string_view NormalMatrixBuffer = "Builtin::NormalMatrixBuffer";
namespace PPLL {
inline constexpr std::string_view Buffer = "Builtin::PPLL::Buffer";
inline constexpr std::string_view Counter = "Builtin::PPLL::Counter";
inline constexpr std::string_view HeadPointerTexture = "Builtin::PPLL::HeadPointerTexture";
}
inline constexpr std::string_view PerMeshBuffer = "Builtin::PerMeshBuffer";
inline constexpr std::string_view PerMeshInstanceBuffer = "Builtin::PerMeshInstanceBuffer";
inline constexpr std::string_view PerObjectBuffer = "Builtin::PerObjectBuffer";
inline constexpr std::string_view PostSkinningVertices = "Builtin::PostSkinningVertices";
inline constexpr std::string_view PreSkinningVertices = "Builtin::PreSkinningVertices";
namespace PrimaryCamera {
inline constexpr std::string_view DepthTexture = "Builtin::PrimaryCamera::DepthTexture";
namespace IndirectCommandBuffers {
inline constexpr std::string_view AlphaTest = "Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest";
inline constexpr std::string_view Blend = "Builtin::PrimaryCamera::IndirectCommandBuffers::Blend";
inline constexpr std::string_view MeshletCullingReset = "Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCullingReset";
inline constexpr std::string_view MeshletFrustrumCulling = "Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletFrustrumCulling";
inline constexpr std::string_view MeshletOcclusionCulling = "Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletOcclusionCulling";
inline constexpr std::string_view Opaque = "Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque";
}
inline constexpr std::string_view LinearDepthMap = "Builtin::PrimaryCamera::LinearDepthMap";
inline constexpr std::string_view MeshletBitfield = "Builtin::PrimaryCamera::MeshletBitfield";
}
namespace Shadows {
inline constexpr std::string_view LinearShadowMaps = "Builtin::Shadows::LinearShadowMaps";
inline constexpr std::string_view ShadowMaps = "Builtin::Shadows::ShadowMaps";
}
}

