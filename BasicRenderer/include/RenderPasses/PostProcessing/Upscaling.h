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
#include "Utilities/MathUtils.h"
#include <ThirdParty/Streamline/sl.h>
#include <ThirdParty/Streamline/sl_consts.h>
#include <ThirdParty/Streamline/sl_dlss.h>
#include <ThirdParty/Streamline/sl_matrix_helpers.h>


inline void StoreFloat4x4(const DirectX::XMMATRIX& m, sl::float4x4& target)
{
    DirectX::XMStoreFloat4(
        reinterpret_cast<DirectX::XMFLOAT4*>(&target.row[0]),
        m.r[0]
    );
    DirectX::XMStoreFloat4(
        reinterpret_cast<DirectX::XMFLOAT4*>(&target.row[1]),
        m.r[1]
    );
    DirectX::XMStoreFloat4(
        reinterpret_cast<DirectX::XMFLOAT4*>(&target.row[2]),
        m.r[2]
    );
    DirectX::XMStoreFloat4(
        reinterpret_cast<DirectX::XMFLOAT4*>(&target.row[3]),
        m.r[3]
    );
}

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
        builder->WithLegacyInterop(Builtin::Color::HDRColorTarget, Builtin::GBuffer::MotionVectors, Builtin::PrimaryCamera::DepthTexture, Builtin::PostProcessing::UpscaledHDR);
    }

    void Setup(const ResourceRegistryView& resourceRegistryView) override {
        m_pHDRTarget = resourceRegistryView.Request<PixelBuffer>(Builtin::Color::HDRColorTarget);
        m_pMotionVectors = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::MotionVectors);
		m_pDepthTexture = resourceRegistryView.Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
		m_pUpscaledHDRTarget = resourceRegistryView.Request<PixelBuffer>(Builtin::PostProcessing::UpscaledHDR);
    }

    void RegisterCommandLists(std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> commandLists) {
        m_renderExtent = { 0, 0, m_renderRes.x, m_renderRes.y };
        m_upscaleExtent = { 0, 0, m_outputRes.x, m_outputRes.y };
        auto viewport = sl::ViewportHandle(0); // 0 is the default viewport

        for (uint32_t i = 0; i < m_numFramesInFlight; i++) {
            sl::Resource colorIn = { sl::ResourceType::eTex2d, (void*)m_pHDRTarget->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
            sl::Resource colorOut = { sl::ResourceType::eTex2d, m_pUpscaledHDRTarget->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
            sl::Resource depth = { sl::ResourceType::eTex2d, m_pDepthTexture->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
            sl::Resource mvec = { sl::ResourceType::eTex2d, m_pMotionVectors->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
            //sl::Resource exposure = { sl::ResourceType::Tex2d, myExposureBuffer, nullptr, nullptr, nullptr }; // TODO

            sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &m_renderExtent };
            sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &m_upscaleExtent };
            sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &m_renderExtent };
            sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &m_renderExtent };
            //sl::ResourceTag exposureTag = sl::ResourceTag{ &exposure, sl::kBufferTypeExposure, sl::ResourceLifecycle::eOnlyValidNow, &my1x1Extent };

            sl::ResourceTag inputs[] = { colorInTag, colorOutTag, depthTag, mvecTag };
            //slSetTagForFrame(*m_frameTokens[i], viewport, inputs, _countof(inputs), commandLists[i].Get());
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

        auto camera = context.currentScene->GetPrimaryCamera().get<Components::Camera>();
		DirectX::XMMATRIX unjitteredProjectionInverse = XMMatrixInverse(nullptr, camera->info.unjitteredProjection);
        sl::float4x4 cameraViewToWorld;
		StoreFloat4x4(camera->info.viewInverse, cameraViewToWorld);
        sl::float4x4 cameraViewToWorldPrev;
		DirectX::XMMATRIX viewPrevInverse = XMMatrixInverse(nullptr, camera->info.prevView);
		StoreFloat4x4(viewPrevInverse, cameraViewToWorldPrev);
        sl::float4x4 cameraViewToPrevCameraView;
        sl::calcCameraToPrevCamera(cameraViewToPrevCameraView, cameraViewToWorld, cameraViewToWorldPrev);
		sl::float4x4 clipToPrevCameraView;

		StoreFloat4x4(camera->info.unjitteredProjection, consts.cameraViewToClip); // Projection matrix
		StoreFloat4x4(unjitteredProjectionInverse, consts.clipToCameraView); // Inverse projection matrix

        sl::matrixMul(clipToPrevCameraView, consts.clipToCameraView, cameraViewToPrevCameraView);

        sl::float4x4 cameraViewToClipPrev;
        StoreFloat4x4(camera->info.unjitteredProjection, cameraViewToClipPrev); // TODO: should we store the actual previous prjection matrix?
		sl::matrixMul(consts.clipToPrevClip, clipToPrevCameraView, cameraViewToClipPrev); // Transform between current and previous clip space
		sl::matrixFullInvert(consts.prevClipToClip, consts.clipToPrevClip); // Transform between previous and current clip space
        consts.jitterOffset.x = camera->jitterPixelSpace.x;
		consts.jitterOffset.y = camera->jitterPixelSpace.y;

        // Set motion vector scaling based on your setup
        //consts.mvecScale = { 1,1 }; // Values in eMotionVectors are in [-1,1] range
        consts.mvecScale = { 1.0f / m_renderRes.x,1.0f / m_renderRes.y }; // Values in eMotionVectors are in pixel space
        consts.cameraPinholeOffset = { 0, 0 };
        //consts.mvecScale = myCustomScaling; // Custom scaling to ensure values end up in [-1,1] range
         
        consts.cameraPos = { camera->info.positionWorldSpace.x, camera->info.positionWorldSpace.y, camera->info.positionWorldSpace.z };

		auto basisVectors = GetBasisVectors3f(camera->info.view);
        consts.cameraUp = { basisVectors.Up.x, basisVectors.Up.y, basisVectors.Up.z };
		consts.cameraRight = { basisVectors.Right.x, basisVectors.Right.y, basisVectors.Right.z };
		consts.cameraFwd = { basisVectors.Forward.x, basisVectors.Forward.y, basisVectors.Forward.z };

        consts.cameraNear = camera->info.zNear;
        consts.cameraFar = camera->info.zFar;
        consts.cameraFOV = camera->info.fov;
        consts.cameraAspectRatio = camera->info.aspectRatio;
        consts.depthInverted = sl::Boolean::eFalse;
        consts.cameraMotionIncluded = sl::Boolean::eTrue;
		consts.motionVectors3D = sl::Boolean::eFalse;
		consts.reset = sl::Boolean::eFalse;
        
        if (SL_FAILED(result, slSetConstants(consts, *frameToken, myViewport))) // constants are changing per frame so frame index is required
        {
			spdlog::error("Failed to set DLSS constants");
        }

        sl::Resource colorIn = { sl::ResourceType::eTex2d, (void*)m_pHDRTarget->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
        sl::Resource colorOut = { sl::ResourceType::eTex2d, m_pUpscaledHDRTarget->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
        sl::Resource depth = { sl::ResourceType::eTex2d, m_pDepthTexture->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
        sl::Resource mvec = { sl::ResourceType::eTex2d, m_pMotionVectors->GetAPIResource(), nullptr, nullptr, D3D12_RESOURCE_STATE_COMMON };
        //sl::Resource exposure = { sl::ResourceType::Tex2d, myExposureBuffer, nullptr, nullptr, nullptr }; // TODO

        sl::ResourceTag colorInTag = sl::ResourceTag{ &colorIn, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent, &m_renderExtent };
        sl::ResourceTag colorOutTag = sl::ResourceTag{ &colorOut, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent, &m_upscaleExtent };
        sl::ResourceTag depthTag = sl::ResourceTag{ &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &m_renderExtent };
        sl::ResourceTag mvecTag = sl::ResourceTag{ &mvec, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &m_renderExtent };

        const sl::BaseStructure* inputs[] = { &myViewport, &depthTag, &mvecTag, &colorInTag, &colorOutTag };

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

    sl::Extent m_renderExtent;
    sl::Extent m_upscaleExtent;

    uint8_t m_numFramesInFlight;

};