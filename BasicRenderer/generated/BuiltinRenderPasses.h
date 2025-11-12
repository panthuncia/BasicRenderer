#pragma once

#include <string_view>
// GENERATED CODE — DO NOT EDIT

struct Engine {
  struct Culling {
    inline static constexpr std::string_view LightClusterCreationPass = "Engine::Culling::LightClusterCreationPass";
    inline static constexpr std::string_view LightCullingPass = "Engine::Culling::LightCullingPass";
    inline static constexpr std::string_view MeshletCullingPass = "Engine::Culling::MeshletCullingPass";
    inline static constexpr std::string_view ObjectCullingPass = "Engine::Culling::ObjectCullingPass";
  };
  struct Misc {
    inline static constexpr std::string_view DebugPass = "Engine::Misc::DebugPass";
  };
  struct OnDemand {
    inline static constexpr std::string_view EnvironmentConversionPass = "Engine::OnDemand::EnvironmentConversionPass";
    inline static constexpr std::string_view EnvironmentFilterPass = "Engine::OnDemand::EnvironmentFilterPass";
    inline static constexpr std::string_view EnvironmentSHPass = "Engine::OnDemand::EnvironmentSHPass";
  };
  struct PostProcessing {
    inline static constexpr std::string_view BloomCompositePass = "Engine::PostProcessing::BloomCompositePass";
    inline static constexpr std::string_view BloomSamplePass = "Engine::PostProcessing::BloomSamplePass";
    inline static constexpr std::string_view LuminanceHistogramAveragePass = "Engine::PostProcessing::LuminanceHistogramAveragePass";
    inline static constexpr std::string_view LuminanceHistogramPass = "Engine::PostProcessing::LuminanceHistogramPass";
    inline static constexpr std::string_view ScreenSpaceReflectionsPass = "Engine::PostProcessing::ScreenSpaceReflectionsPass";
    inline static constexpr std::string_view SpecularIBLPass = "Engine::PostProcessing::SpecularIBLPass";
    inline static constexpr std::string_view ToneMappingPass = "Engine::PostProcessing::ToneMappingPass";
    inline static constexpr std::string_view UpscalePass = "Engine::PostProcessing::UpscalePass";
  };
  struct Primary {
    inline static constexpr std::string_view DeferredShadingPass = "Engine::Primary::DeferredShadingPass";
    inline static constexpr std::string_view ForwardPass = "Engine::Primary::ForwardPass";
    inline static constexpr std::string_view GBufferPass = "Engine::Primary::GBufferPass";
    inline static constexpr std::string_view OITAccumulationPass = "Engine::Primary::OITAccumulationPass";
    inline static constexpr std::string_view OITResolvePass = "Engine::Primary::OITResolvePass";
    inline static constexpr std::string_view ShadowMapsPass = "Engine::Primary::ShadowMapsPass";
    inline static constexpr std::string_view SkinningPass = "Engine::Primary::SkinningPass";
    inline static constexpr std::string_view SkyboxPass = "Engine::Primary::SkyboxPass";
    inline static constexpr std::string_view ZPrepass = "Engine::Primary::ZPrepass";
  };
};

