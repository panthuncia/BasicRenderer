#ifndef BUILTIN_RESOURCES_H
#define BUILTIN_RESOURCES_H

// GENERATED CODE — DO NOT EDIT

struct Builtin {
  struct ActiveDrawSetIndices {
    string AlphaTest = "Builtin::ActiveDrawSetIndices::AlphaTest";
    string Blend = "Builtin::ActiveDrawSetIndices::Blend";
    string Opaque = "Builtin::ActiveDrawSetIndices::Opaque";
  };
  string BRDFLUT = "Builtin::BRDFLUT";
  string CameraBuffer = "Builtin::CameraBuffer";
  struct Color {
    string HDRColorTarget = "Builtin::Color::HDRColorTarget";
    string SRGBColorTarget = "Builtin::Color::SRGBColorTarget";
  };
  string DebugTexture = "Builtin::DebugTexture";
  struct Environment {
    string CurrentCubemap = "Builtin::Environment::CurrentCubemap";
    string CurrentPrefilteredCubemap = "Builtin::Environment::CurrentPrefilteredCubemap";
    string InfoBuffer = "Builtin::Environment::InfoBuffer";
    string PrefilteredCubemapsGroup = "Builtin::Environment::PrefilteredCubemapsGroup";
    string WorkingCubemapGroup = "Builtin::Environment::WorkingCubemapGroup";
    string WorkingHDRIGroup = "Builtin::Environment::WorkingHDRIGroup";
  };
  struct GBuffer {
    string Albedo = "Builtin::GBuffer::Albedo";
    string Emissive = "Builtin::GBuffer::Emissive";
    string MetallicRoughness = "Builtin::GBuffer::MetallicRoughness";
    string MotionVectors = "Builtin::GBuffer::MotionVectors";
    string Normals = "Builtin::GBuffer::Normals";
  };
  struct GTAO {
    string OutputAOTerm = "Builtin::GTAO::OutputAOTerm";
    string WorkingAOTerm1 = "Builtin::GTAO::WorkingAOTerm1";
    string WorkingAOTerm2 = "Builtin::GTAO::WorkingAOTerm2";
    string WorkingDepths = "Builtin::GTAO::WorkingDepths";
    string WorkingEdges = "Builtin::GTAO::WorkingEdges";
  };
  struct IndirectCommandBuffers {
    string AlphaTest = "Builtin::IndirectCommandBuffers::AlphaTest";
    string Blend = "Builtin::IndirectCommandBuffers::Blend";
    string Master = "Builtin::IndirectCommandBuffers::Master";
    string MeshletCulling = "Builtin::IndirectCommandBuffers::MeshletCulling";
    string Opaque = "Builtin::IndirectCommandBuffers::Opaque";
  };
  struct Light {
    string ActiveLightIndices = "Builtin::Light::ActiveLightIndices";
    string BufferGroup = "Builtin::Light::BufferGroup";
    string ClusterBuffer = "Builtin::Light::ClusterBuffer";
    string DirectionalLightCascadeBuffer = "Builtin::Light::DirectionalLightCascadeBuffer";
    string InfoBuffer = "Builtin::Light::InfoBuffer";
    string PagesBuffer = "Builtin::Light::PagesBuffer";
    string PagesCounter = "Builtin::Light::PagesCounter";
    string PointLightCubemapBuffer = "Builtin::Light::PointLightCubemapBuffer";
    string SpotLightMatrixBuffer = "Builtin::Light::SpotLightMatrixBuffer";
    string ViewResourceGroup = "Builtin::Light::ViewResourceGroup";
  };
  string MeshInstanceMeshletCullingBitfieldGroup = "Builtin::MeshInstanceMeshletCullingBitfieldGroup";
  string MeshInstanceOcclusionCullingBitfieldGroup = "Builtin::MeshInstanceOcclusionCullingBitfieldGroup";
  struct MeshResources {
    string MeshletBounds = "Builtin::MeshResources::MeshletBounds";
    string MeshletOffsets = "Builtin::MeshResources::MeshletOffsets";
    string MeshletTriangles = "Builtin::MeshResources::MeshletTriangles";
    string MeshletVertexIndices = "Builtin::MeshResources::MeshletVertexIndices";
  };
  string MeshletCullingBitfieldGroup = "Builtin::MeshletCullingBitfieldGroup";
  string NormalMatrixBuffer = "Builtin::NormalMatrixBuffer";
  struct PPLL {
    string Counter = "Builtin::PPLL::Counter";
    string DataBuffer = "Builtin::PPLL::DataBuffer";
    string HeadPointerTexture = "Builtin::PPLL::HeadPointerTexture";
  };
  string PerMeshBuffer = "Builtin::PerMeshBuffer";
  string PerMeshInstanceBuffer = "Builtin::PerMeshInstanceBuffer";
  string PerObjectBuffer = "Builtin::PerObjectBuffer";
  struct PostProcessing {
    string ScreenSpaceReflections = "Builtin::PostProcessing::ScreenSpaceReflections";
    string UpscaledHDR = "Builtin::PostProcessing::UpscaledHDR";
  };
  string PostSkinningVertices = "Builtin::PostSkinningVertices";
  string PreSkinningVertices = "Builtin::PreSkinningVertices";
  struct PrimaryCamera {
    string DepthTexture = "Builtin::PrimaryCamera::DepthTexture";
    struct IndirectCommandBuffers {
      string AlphaTest = "Builtin::PrimaryCamera::IndirectCommandBuffers::AlphaTest";
      string Blend = "Builtin::PrimaryCamera::IndirectCommandBuffers::Blend";
      string MeshletCulling = "Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCulling";
      string MeshletCullingReset = "Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletCullingReset";
      string MeshletFrustrumCulling = "Builtin::PrimaryCamera::IndirectCommandBuffers::MeshletFrustrumCulling";
      string Opaque = "Builtin::PrimaryCamera::IndirectCommandBuffers::Opaque";
    };
    string LinearDepthMap = "Builtin::PrimaryCamera::LinearDepthMap";
    string MeshletBitfield = "Builtin::PrimaryCamera::MeshletBitfield";
  };
  struct Shadows {
    string LinearShadowMaps = "Builtin::Shadows::LinearShadowMaps";
    string ShadowMaps = "Builtin::Shadows::ShadowMaps";
  };
};


#endif // BUILTIN_RESOURCES_H
