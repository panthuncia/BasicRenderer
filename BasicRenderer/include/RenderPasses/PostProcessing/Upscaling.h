#pragma once

#include <unordered_map>
#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Mesh/Mesh.h"
#include "Scene/Scene.h"
#include "Resources/TextureDescription.h"
#include "Managers/Singletons/UploadManager.h"
#include "Managers/Singletons/UpscalingManager.h"
#include "Utilities/MathUtils.h"

class UpscalingPass : public RenderPass {
public:
    UpscalingPass() {
        m_renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
		m_outputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) {
        builder->WithLegacyInterop(Builtin::Color::HDRColorTarget, Builtin::GBuffer::MotionVectors, Builtin::PrimaryCamera::DepthTexture, Builtin::PostProcessing::UpscaledHDR);
    }

    void Setup(const ResourceRegistryView& resourceRegistryView) override {
        m_pHDRTarget = resourceRegistryView.Request<PixelBuffer>(Builtin::Color::HDRColorTarget);
        m_pMotionVectors = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::MotionVectors);
		m_pDepthTexture = resourceRegistryView.Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
		m_pUpscaledHDRTarget = resourceRegistryView.Request<PixelBuffer>(Builtin::PostProcessing::UpscaledHDR);
    }

    void RegisterCommandLists(std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> commandLists) {
    }


    PassReturn Execute(RenderContext& context) override {
        UpscalingManager::GetInstance().Evaluate(context, m_pHDRTarget.get(), m_pUpscaledHDRTarget.get(), m_pDepthTexture.get(), m_pMotionVectors.get());
        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup the render pass
    }

private:

    std::shared_ptr<PixelBuffer> m_pHDRTarget;
    std::shared_ptr<PixelBuffer> m_pMotionVectors;
	std::shared_ptr<PixelBuffer> m_pDepthTexture;
	std::shared_ptr<PixelBuffer> m_pUpscaledHDRTarget;

    DirectX::XMUINT2 m_renderRes;
    DirectX::XMUINT2 m_outputRes;

};