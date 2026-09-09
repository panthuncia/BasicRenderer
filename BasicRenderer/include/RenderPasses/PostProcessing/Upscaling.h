#pragma once

#include <functional>

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "Scene/Scene.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/UpscalingManager.h"
#include "Render/PreparedPass.h"

struct UpscalingFrameData {
    Components::Camera camera;
    uint64_t frameNumber = 0;
    double deltaTime = 0;
    std::shared_ptr<PixelBuffer> hdr;
    std::shared_ptr<PixelBuffer> output;
    std::shared_ptr<PixelBuffer> depth;
    std::shared_ptr<PixelBuffer> motion;
};

class UpscalingPass
    : public org::TypedRenderGraphPass<UpscalingPass, UpscalingFrameData> {
public:
    UpscalingPass() {
        m_renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
		m_outputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();
    }

    void Declare(org::PassBuilder& declaration) {
        auto* builder = &declaration;
        // Upscalers produce only the full-resolution image in mip 0. Keep the
        // interop contract explicit so it remains correct if this output is
        // replaced by an external resource with additional subresources.
        const auto upscaledHDR = Subresources(
            Builtin::PostProcessing::UpscaledHDR,
            Mip{ 0, 1 });
        const UpscalingMode upscalingMode = UpscalingManager::GetInstance().GetCurrentUpscalingMode();
        const rhi::Backend backend = DeviceManager::GetInstance().GetBackend();
        const bool useDilatedMotionVectors = upscalingMode == UpscalingMode::DLSS &&
            SettingsManager::GetInstance().getSettingGetter<bool>("enableDilatedMotionVectors")();
        const auto motionVectors = useDilatedMotionVectors
            ? Builtin::Surface::DilatedMotion
            : Builtin::Surface::Motion;

        if (upscalingMode == UpscalingMode::FSR3) {
            builder->WithShaderResource(
                Builtin::Color::HDRColorTarget,
                motionVectors,
                Builtin::PrimaryCamera::ProjectedDepthTexture)
                .WithUnorderedAccess(upscaledHDR);
            return;
        }

        if (upscalingMode == UpscalingMode::None && backend == rhi::Backend::Vulkan) {
            builder->WithCopySource(Builtin::Color::HDRColorTarget)
                .WithCopyDest(upscaledHDR)
                .WithShaderResource(
                    Builtin::Surface::Motion,
                    Builtin::PrimaryCamera::ProjectedDepthTexture);
            return;
        }

        ResourceState vulkanStreamlineExitState{
            .access = rhi::ResourceAccessType::UnorderedAccess | rhi::ResourceAccessType::UnorderedAccessClear,
            .layout = rhi::ResourceLayout::UnorderedAccess,
            .sync = rhi::ResourceSyncState::AllShading | rhi::ResourceSyncState::ClearUnorderedAccessView };
        ResourceState dx12StreamlineExitState{
            .access = rhi::ResourceAccessType::Common,
            .layout = rhi::ResourceLayout::Common,
            .sync = rhi::ResourceSyncState::All };

        // TODO: Remove these backend-specific workarounds when ORG can model combined non-conflicting usages on one resource.
        if (backend == rhi::Backend::Vulkan) {
            builder->WithShaderResource(
                Builtin::Color::HDRColorTarget,
                motionVectors,
                Builtin::PrimaryCamera::ProjectedDepthTexture)
                .WithUnorderedAccessClear(upscaledHDR)
                .WithInternalTransition(upscaledHDR, vulkanStreamlineExitState);
        }
        else {
            builder->WithLegacyInterop(
                Builtin::Color::HDRColorTarget,
                motionVectors,
                Builtin::PrimaryCamera::ProjectedDepthTexture,
                upscaledHDR)
                .WithInternalTransition(upscaledHDR, dx12StreamlineExitState);
        }
    }

    void Initialize() {
        m_pHDRTarget = m_resourceRegistryView->RequestSharedAs<PixelBuffer>(Builtin::Color::HDRColorTarget);
        const bool useDilatedMotionVectors =
            UpscalingManager::GetInstance().GetCurrentUpscalingMode() == UpscalingMode::DLSS &&
            SettingsManager::GetInstance().getSettingGetter<bool>("enableDilatedMotionVectors")();
        const auto motionVectors = useDilatedMotionVectors
            ? Builtin::Surface::DilatedMotion
            : Builtin::Surface::Motion;
        m_pMotionVectors = m_resourceRegistryView->RequestSharedAs<PixelBuffer>(motionVectors);
		m_pDepthTexture = m_resourceRegistryView->RequestSharedAs<PixelBuffer>(Builtin::PrimaryCamera::ProjectedDepthTexture);
		m_pUpscaledHDRTarget = m_resourceRegistryView->RequestSharedAs<PixelBuffer>(Builtin::PostProcessing::UpscaledHDR);
    }

    UpscalingFrameData Prepare(const org::PassPrepareContext& preparation) {
        const auto* context = preparation.preparationData->Get<UpdateContext>();
        return UpscalingFrameData{
            .camera = context->primaryCamera,
            .frameNumber = preparation.frameNumber,
            .deltaTime = preparation.deltaTime,
            .hdr = m_pHDRTarget,
            .output = m_pUpscaledHDRTarget,
            .depth = m_pDepthTexture,
            .motion = m_pMotionVectors,
        };
    }
    static void Record(const UpscalingFrameData& data, org::PassRecordContext& recording) {
        UpscalingManager::GetInstance().Evaluate(recording.Commands(), &data.camera,
            data.frameNumber, data.deltaTime, data.hdr.get(), data.output.get(),
            data.depth.get(), data.motion.get());
    }

private:

    std::shared_ptr<PixelBuffer> m_pHDRTarget;
    std::shared_ptr<PixelBuffer> m_pMotionVectors;
	std::shared_ptr<PixelBuffer> m_pDepthTexture;
	std::shared_ptr<PixelBuffer> m_pUpscaledHDRTarget;

    DirectX::XMUINT2 m_renderRes;
    DirectX::XMUINT2 m_outputRes;

};
