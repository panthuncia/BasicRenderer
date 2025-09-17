#pragma once

#include <string_view>
#include <refl.hpp>

// GENERATED CODE — DO NOT EDIT

struct Builtin {
  struct ActiveDrawSetIndices {
    inline static constexpr std::string_view AlphaTest = "Builtin::ActiveDrawSetIndices::AlphaTest";
    inline static constexpr std::string_view Blend = "Builtin::ActiveDrawSetIndices::Blend";
    inline static constexpr std::string_view Opaque = "Builtin::ActiveDrawSetIndices::Opaque";
  };
  inline static constexpr std::string_view BRDFLUT = "Builtin::BRDFLUT";
  inline static constexpr std::string_view CameraBuffer = "Builtin::CameraBuffer";
  struct Color {
    inline static constexpr std::string_view HDRColorTarget = "Builtin::Color::HDRColorTarget";
    inline static constexpr std::string_view SRGBColorTarget = "Builtin::Color::SRGBColorTarget";
  };
  inline static constexpr std::string_view DebugTexture = "Builtin::DebugTexture";
  struct Environment {
    inline static constexpr std::string_view CurrentCubemap = "Builtin::Environment::CurrentCubemap";
    inline static constexpr std::string_view CurrentPrefilteredCubemap = "Builtin::Environment::CurrentPrefilteredCubemap";
    inline static constexpr std::string_view InfoBuffer = "Builtin::Environment::InfoBuffer";
    inline static constexpr std::string_view PrefilteredCubemapsGroup = "Builtin::Environment::PrefilteredCubemapsGroup";
    inline static constexpr std::string_view WorkingCubemapGroup = "Builtin::Environment::WorkingCubemapGroup";
    inline static constexpr std::string_view WorkingHDRIGroup = "Builtin::Environment::WorkingHDRIGroup";
  };
  struct GBuffer {
    inline static constexpr std::string_view Albedo = "Builtin::GBuffer::Albedo";
    inline static constexpr std::string_view Emissive = "Builtin::GBuffer::Emissive";
    inline static constexpr std::string_view MetallicRoughness = "Builtin::GBuffer::MetallicRoughness";
    inline static constexpr std::string_view MotionVectors = "Builtin::GBuffer::MotionVectors";
    inline static constexpr std::string_view Normals = "Builtin::GBuffer::Normals";
  };
  struct GTAO {
    inline static constexpr std::string_view OutputAOTerm = "Builtin::GTAO::OutputAOTerm";
    inline static constexpr std::string_view WorkingAOTerm1 = "Builtin::GTAO::WorkingAOTerm1";
    inline static constexpr std::string_view WorkingAOTerm2 = "Builtin::GTAO::WorkingAOTerm2";
    inline static constexpr std::string_view WorkingDepths = "Builtin::GTAO::WorkingDepths";
    inline static constexpr std::string_view WorkingEdges = "Builtin::GTAO::WorkingEdges";
  };
  struct IndirectCommandBuffers {
    inline static constexpr std::string_view AlphaTest = "Builtin::IndirectCommandBuffers::AlphaTest";
    inline static constexpr std::string_view Blend = "Builtin::IndirectCommandBuffers::Blend";
    inline static constexpr std::string_view Master = "Builtin::IndirectCommandBuffers::Master";
    inline static constexpr std::string_view MeshletCulling = "Builtin::IndirectCommandBuffers::MeshletCulling";
    inline static constexpr std::string_view Opaque = "Builtin::IndirectCommandBuffers::Opaque";
  };
  struct Light {
    inline static constexpr std::string_view ActiveLightIndices = "Builtin::Light::ActiveLightIndices";
    inline static constexpr std::string_view BufferGroup = "Builtin::Light::BufferGroup";
    inline static constexpr std::string_view ClusterBuffer = "Builtin::Light::ClusterBuffer";
    inline static constexpr std::string_view DirectionalLightCascadeBuffer = "Builtin::Light::DirectionalLightCascadeBuffer";
    inline static constexpr std::string_view InfoBuffer = "Builtin::Light::InfoBuffer";
    inline static constexpr std::string_view PagesBuffer = "Builtin::Light::PagesBuffer";
    inline static constexpr std::string_view PagesCounter = "Builtin::Light::PagesCounter";
    inline static constexpr std::string_view PointLightCubemapBuffer = "Builtin::Light::PointLightCubemapBuffer";
    inline static constexpr std::string_view SpotLightMatrixBuffer = "Builtin::Light::SpotLightMatrixBuffer";
    inline static constexpr std::string_view ViewResourceGroup = "Builtin::Light::ViewResourceGroup";
  };
  inline static constexpr std::string_view MeshInstanceMeshletCullingBitfieldGroup = "Builtin::MeshInstanceMeshletCullingBitfieldGroup";
  inline static constexpr std::string_view MeshInstanceOcclusionCullingBitfieldGroup = "Builtin::MeshInstanceOcclusionCullingBitfieldGroup";
  struct MeshResources {
    inline static constexpr std::string_view MeshletBounds = "Builtin::MeshResources::MeshletBounds";
    inline static constexpr std::string_view MeshletOffsets = "Builtin::MeshResources::MeshletOffsets";
    inline static constexpr std::string_view MeshletTriangles = "Builtin::MeshResources::MeshletTriangles";
    inline static constexpr std::string_view MeshletVertexIndices = "Builtin::MeshResources::MeshletVertexIndices";
  };
  inline static constexpr std::string_view MeshletCullingBitfieldGroup = "Builtin::MeshletCullingBitfieldGroup";
  inline static constexpr std::string_view NormalMatrixBuffer = "Builtin::NormalMatrixBuffer";
  struct PPLL {
    inline static constexpr std::string_view Counter = "Builtin::PPLL::Counter";
    inline static constexpr std::string_view DataBuffer = "Builtin::PPLL::DataBuffer";
    inline static constexpr std::string_view HeadPointerTexture = "Builtin::PPLL::HeadPointerTexture";
  };
  inline static constexpr std::string_view PerMeshBuffer = "Builtin::PerMeshBuffer";
  inline static constexpr std::string_view PerMeshInstanceBuffer = "Builtin::PerMeshInstanceBuffer";
  inline static constexpr std::string_view PerObjectBuffer = "Builtin::PerObjectBuffer";
  struct PostProcessing {
    inline static constexpr std::string_view AdaptedLuminance = "Builtin::PostProcessing::AdaptedLuminance";
    inline static constexpr std::string_view LuminanceHistogram = "Builtin::PostProcessing::LuminanceHistogram";
    inline static constexpr std::string_view ScreenSpaceReflections = "Builtin::PostProcessing::ScreenSpaceReflections";
    inline static constexpr std::string_view UpscaledHDR = "Builtin::PostProcessing::UpscaledHDR";
  };
  inline static constexpr std::string_view PostSkinningVertices = "Builtin::PostSkinningVertices";
  inline static constexpr std::string_view PreSkinningVertices = "Builtin::PreSkinningVertices";
  struct PrimaryCamera {
    inline static constexpr std::string_view DepthTexture = "Builtin::PrimaryCamera::DepthTexture";
    struct IndirectCommandBuffers {
      inline static constexpr std::string_view AlphaTest = "Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest";
      inline static constexpr std::string_view Blend = "Builtin::PrimaryCamera::IndirectCommandBuffers::Blend";
      inline static constexpr std::string_view MeshletCulling = "Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCulling";
      inline static constexpr std::string_view MeshletCullingReset = "Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCullingReset";
      inline static constexpr std::string_view Opaque = "Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque";
    };
    inline static constexpr std::string_view LinearDepthMap = "Builtin::PrimaryCamera::LinearDepthMap";
    inline static constexpr std::string_view MeshletBitfield = "Builtin::PrimaryCamera::MeshletBitfield";
    inline static constexpr std::string_view VisibilityTexture = "Builtin::PrimaryCamera::VisibilityTexture";
  };
  struct Shadows {
    inline static constexpr std::string_view LinearShadowMaps = "Builtin::Shadows::LinearShadowMaps";
    inline static constexpr std::string_view ShadowMaps = "Builtin::Shadows::ShadowMaps";
  };
};

// refl-cpp registration
REFL_AUTO(
  type(Builtin)
);
REFL_AUTO(
  type(Builtin::ActiveDrawSetIndices)
);
REFL_AUTO(
  type(Builtin::Color)
);
REFL_AUTO(
  type(Builtin::Environment)
);
REFL_AUTO(
  type(Builtin::GBuffer)
);
REFL_AUTO(
  type(Builtin::GTAO)
);
REFL_AUTO(
  type(Builtin::IndirectCommandBuffers)
);
REFL_AUTO(
  type(Builtin::Light)
);
REFL_AUTO(
  type(Builtin::MeshResources)
);
REFL_AUTO(
  type(Builtin::PPLL)
);
REFL_AUTO(
  type(Builtin::PostProcessing)
);
REFL_AUTO(
  type(Builtin::PrimaryCamera)
);
REFL_AUTO(
  type(Builtin::PrimaryCamera::IndirectCommandBuffers)
);
REFL_AUTO(
  type(Builtin::Shadows)
);
