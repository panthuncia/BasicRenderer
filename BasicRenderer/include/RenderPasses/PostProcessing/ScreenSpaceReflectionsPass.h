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
#include "Managers/Singletons/FFXManager.h"
#include "Utilities/MathUtils.h"

class ScreenSpaceReflectionsPass : public RenderPass {
public:
    ScreenSpaceReflectionsPass() {
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) {
        builder->WithLegacyInterop(Builtin::Color::HDRColorTarget,
            Builtin::GBuffer::MotionVectors,
            Builtin::PrimaryCamera::DepthTexture,
            Builtin::GBuffer::Normals,
            Builtin::GBuffer::MetallicRoughness,
            Builtin::GBuffer::MotionVectors,
            Builtin::Environment::CurrentCubemap,
            Builtin::BRDFLUT,
            Builtin::PostProcessing::ScreenSpaceReflections);
    }

    void Setup(const ResourceRegistryView& resourceRegistryView) override {
        m_pHDRTarget = resourceRegistryView.Request<PixelBuffer>(Builtin::Color::HDRColorTarget);
        m_pMotionVectors = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::MotionVectors);
        m_pDepthTexture = resourceRegistryView.Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
		m_pNormals = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::Normals);
        m_pMetallicRoughness = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::MetallicRoughness);
		m_pMotionVectors = resourceRegistryView.Request<PixelBuffer>(Builtin::GBuffer::MotionVectors);
		m_pEnvironmentCubemap = resourceRegistryView.Request<PixelBuffer>(Builtin::Environment::CurrentCubemap);
        m_pBRDFLUT = resourceRegistryView.Request<PixelBuffer>(Builtin::BRDFLUT);
		m_pSSSROutput = resourceRegistryView.Request<PixelBuffer>(Builtin::PostProcessing::ScreenSpaceReflections);
    }

    void RegisterCommandLists(std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> commandLists) {
    }


    PassReturn Execute(RenderContext& context) override {
        FFXManager::GetInstance().EvaluateSSSR(
            context,
            m_pHDRTarget.get(),
            m_pDepthTexture.get(),
			m_pNormals.get(),
			m_pMetallicRoughness.get(),
			m_pMotionVectors.get(),
            m_pEnvironmentCubemap.get(),
			m_pBRDFLUT.get(),
            m_pSSSROutput.get()
		);
        return {};
    }

    void Cleanup(RenderContext& context) override {
        // Cleanup the render pass
    }

private:

    std::shared_ptr<PixelBuffer> m_pHDRTarget;
    std::shared_ptr<PixelBuffer> m_pMotionVectors;
    std::shared_ptr<PixelBuffer> m_pDepthTexture;
	std::shared_ptr<PixelBuffer> m_pNormals;
	std::shared_ptr<PixelBuffer> m_pMetallicRoughness;
	std::shared_ptr<PixelBuffer> m_pEnvironmentCubemap;
	std::shared_ptr<PixelBuffer> m_pBRDFLUT;
	std::shared_ptr<PixelBuffer> m_pSSSROutput;
};