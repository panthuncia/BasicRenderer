#pragma once

#include <functional>

#include "RenderPasses/Base/RenderPass.h"
#include "Scene/Scene.h"
#include "Managers/Singletons/UpscalingManager.h"

class UpscalingPass : public RenderPass {
public:
    UpscalingPass() {
        m_renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
		m_outputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) {
        builder->WithLegacyInterop(Builtin::Color::HDRColorTarget, Builtin::GBuffer::MotionVectors, Builtin::PrimaryCamera::DepthTexture, Builtin::PostProcessing::UpscaledHDR);
    }

    void Setup() override {
        m_pHDRTarget = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::Color::HDRColorTarget);
        m_pMotionVectors = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::GBuffer::MotionVectors);
		m_pDepthTexture = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
		m_pUpscaledHDRTarget = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PostProcessing::UpscaledHDR);
    }

    PassReturn Execute(RenderContext& context) override {
        UpscalingManager::GetInstance().Evaluate(context, m_pHDRTarget, m_pUpscaledHDRTarget, m_pDepthTexture, m_pMotionVectors);
        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup the render pass
    }

private:

    PixelBuffer* m_pHDRTarget;
    PixelBuffer* m_pMotionVectors;
	PixelBuffer* m_pDepthTexture;
	PixelBuffer* m_pUpscaledHDRTarget;

    DirectX::XMUINT2 m_renderRes;
    DirectX::XMUINT2 m_outputRes;

};