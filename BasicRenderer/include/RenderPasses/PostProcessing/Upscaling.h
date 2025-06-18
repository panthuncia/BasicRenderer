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
#include <ThirdParty/Streamline/sl.h>
#include <ThirdParty/Streamline/sl_consts.h>
#include <ThirdParty/Streamline/sl_dlss.h>

class UpscalingPass : public RenderPass {
public:
    UpscalingPass() {
        m_renderRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("renderResolution")();
		m_outputRes = SettingsManager::GetInstance().getSettingGetter<DirectX::XMUINT2>("outputResolution")();
        m_numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
        m_frameTokens.resize(m_numFramesInFlight);
        for (uint32_t i = 0; i < m_numFramesInFlight; i++) {
            slGetNewFrameToken(m_frameTokens[i], &i);
        }
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) {
        builder->WithShaderResource(Builtin::Color::HDRColorTarget, Builtin::GBuffer::MotionVectors, Builtin::PrimaryCamera::DepthTexture);
		builder->WithUnorderedAccess(Builtin::PostProcessing::UpscaledHDR);
    }

    void Setup(const ResourceRegistryView& resourceRegistryView) override {
        m_pHDRTarget = resourceRegistryView.Request<PixelBuffer>(Builtin::Color::HDRColorTarget);
        m_pMotionVectors = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::MotionVectors);
		m_pDepthTexture = resourceRegistryView.Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
		m_pUpscaledHDRTarget = resourceRegistryView.Request<PixelBuffer>(Builtin::PostProcessing::UpscaledHDR);
    }

    void RegisterCommandLists(std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> commandLists) {
        sl::Extent myExtent = { m_renderRes.x, m_renderRes.y };
        sl::Extent fullExtent = { m_outputRes.x, m_outputRes.y };
        auto viewport = sl::ViewportHandle(0); // 0 is the default viewport

        for (uint32_t i = 0; i < m_numFramesInFlight; i++) {
            sl::Resource colorIn = { sl::ResourceType::eTex2d, (void*)m_pHDRTarget->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
            sl::Resource colorOut = { sl::ResourceType::eTex2d, m_pUpscaledHDRTarget->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON};
            sl::Resource depth = { sl::ResourceType::eTex2d, m_pDepthTexture->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON};
            sl::Resource mvec = { sl::ResourceType::eTex2d, m_pMotionVectors->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
            //sl::Resource exposure = { sl::ResourceType::Tex2d, myExposureBuffer, nullptr, nullptr, nullptr }; // TODO

            sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &myExtent };
            sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &fullExtent };
            sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &myExtent };
            sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &myExtent };
            //sl::ResourceTag exposureTag = sl::ResourceTag{ &exposure, sl::kBufferTypeExposure, sl::ResourceLifecycle::eOnlyValidNow, &my1x1Extent };

            sl::ResourceTag inputs[] = { colorInTag, colorOutTag, depthTag, mvecTag };
            slSetTagForFrame(*m_frameTokens[i], viewport, inputs, _countof(inputs), commandLists[i].Get());
        }

        sl::DLSSOptions dlssOptions = {};
        // Set preferred Render Presets per Perf Quality Mode. These are typically set one time
        // and established while evaluating DLSS SR Image Quality for your Application.
        // It will be set to DSSPreset::eDefault if unspecified.
        // Please Refer to section 3.12 of the DLSS Programming Guide for details.
        dlssOptions.dlaaPreset = sl::DLSSPreset::ePresetK;
        dlssOptions.qualityPreset = sl::DLSSPreset::ePresetK;
        dlssOptions.balancedPreset = sl::DLSSPreset::ePresetK;
        dlssOptions.performancePreset = sl::DLSSPreset::ePresetK;
        dlssOptions.ultraPerformancePreset = sl::DLSSPreset::ePresetF;
        // These are populated based on user selection in the UI
        dlssOptions.mode = sl::DLSSMode::eBalanced;
		dlssOptions.outputWidth = m_outputRes.x;
        dlssOptions.outputHeight = m_outputRes.y;
        dlssOptions.sharpness = 0;
        dlssOptions.colorBuffersHDR = sl::Boolean::eTrue; // assuming HDR pipeline
        dlssOptions.useAutoExposure = sl::Boolean::eTrue; // autoexposure is not to be used if a proper exposure texture is available
        dlssOptions.alphaUpscalingEnabled = sl::Boolean::eFalse; // experimental alpha upscaling, enable to upscale alpha channel of color texture
        if (SL_FAILED(result, slDLSSSetOptions(viewport, dlssOptions)))
        {
            // Handle error here, check the logs
        }

    }


    PassReturn Execute(RenderContext& context) override {
		auto frameToken = m_frameTokens[context.frameIndex];
		auto myViewport = sl::ViewportHandle(0); // 0 is the default viewport
        sl::Constants consts = {};
        // Set motion vector scaling based on your setup
        consts.mvecScale = { 1,1 }; // Values in eMotionVectors are in [-1,1] range
        consts.mvecScale = { 1.0f / m_renderRes.x,1.0f / m_renderRes.y }; // Values in eMotionVectors are in pixel space
        //consts.mvecScale = myCustomScaling; // Custom scaling to ensure values end up in [-1,1] range
        // Set all other constants here
        if (SL_FAILED(result, slSetConstants(consts, *frameToken, myViewport))) // constants are changing per frame so frame index is required
        {
			spdlog::error("Failed to set DLSS constants");
        }

        const sl::BaseStructure* inputs[] = { &myViewport };
        if (SL_FAILED(result, slEvaluateFeature(sl::kFeatureDLSS, *frameToken, inputs, _countof(inputs), context.commandList)))
        {
            spdlog::error("DLSS evaluation failed!");
        }
        else
        {
            // IMPORTANT: Host is responsible for restoring state on the command list used
            //restoreState(myCmdList); ??
        }

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

    std::vector<sl::FrameToken*> m_frameTokens; // Frame tokens for each frame in flight

    DirectX::XMUINT2 m_renderRes;
    DirectX::XMUINT2 m_outputRes;
    uint8_t m_numFramesInFlight;

};