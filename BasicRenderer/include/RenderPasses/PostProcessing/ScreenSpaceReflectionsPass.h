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
            Builtin::Environment::CurrentPrefilteredCubemap,
            Builtin::BRDFLUT,
            Builtin::PostProcessing::ScreenSpaceReflections);

        builder->WithInternalTransition({ Builtin::PostProcessing::ScreenSpaceReflections, {} }, {ResourceAccessType::COMMON, ResourceLayout::LAYOUT_COMMON, ResourceSyncState::ALL});
    }

    void Setup() override {
        m_pHDRTarget = m_resourceRegistryView->Request<PixelBuffer>(Builtin::Color::HDRColorTarget);
        m_pMotionVectors = m_resourceRegistryView->Request<PixelBuffer>(Builtin::GBuffer::MotionVectors);
        m_pDepthTexture = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
		m_pNormals = m_resourceRegistryView->Request<PixelBuffer>(Builtin::GBuffer::Normals);
        m_pMetallicRoughness = m_resourceRegistryView->Request<PixelBuffer>(Builtin::GBuffer::MetallicRoughness);
		m_pMotionVectors = m_resourceRegistryView->Request<PixelBuffer>(Builtin::GBuffer::MotionVectors);
		m_pEnvironmentCubemap = m_resourceRegistryView->Request<Texture>(Builtin::Environment::CurrentPrefilteredCubemap);
        m_pBRDFLUT = m_resourceRegistryView->Request<PixelBuffer>(Builtin::BRDFLUT);
		m_pSSSROutput = m_resourceRegistryView->Request<PixelBuffer>(Builtin::PostProcessing::ScreenSpaceReflections);
    }

    void RegisterCommandLists(std::vector<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList7>> commandLists) {
    }


    PassReturn Execute(RenderContext& context) override {

		// Clear the render target of the SSSR output
        //context.commandList->ClearRenderTargetView(
        //    m_pSSSROutput->GetRTVInfo(0).cpuHandle,
        //    &m_pSSSROutput->GetClearColor()[0],
        //    0,
        //    nullptr
        //);

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            context.textureDescriptorHeap,
            context.samplerDescriptorHeap,
        };
        context.commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		// Transition SSSR output to UAV state
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_pSSSROutput->GetAPIResource(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS
		);
		context.commandList->ResourceBarrier(1, &barrier);

        // Clear as UAV
        context.commandList->ClearUnorderedAccessViewFloat(
            m_pSSSROutput->GetUAVShaderVisibleInfo(0).gpuHandle,
            m_pSSSROutput->GetUAVNonShaderVisibleInfo(0).cpuHandle,
            m_pSSSROutput->GetAPIResource(),
            &m_pSSSROutput->GetClearColor()[0],
            0,
            nullptr
		);

        FFXManager::GetInstance().EvaluateSSSR(
            context,
            m_pHDRTarget.get(),
            m_pDepthTexture.get(),
			m_pNormals.get(),
			m_pMetallicRoughness.get(),
			m_pMotionVectors.get(),
            m_pEnvironmentCubemap->GetBuffer().get(),
			m_pBRDFLUT.get(),
            m_pSSSROutput.get()
		);

		// All resources must exit in COMMON state for legacy interop

		barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_pSSSROutput->GetAPIResource(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COMMON
        );

		context.commandList->ResourceBarrier(1, &barrier);

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
	std::shared_ptr<Texture> m_pEnvironmentCubemap;
	std::shared_ptr<PixelBuffer> m_pBRDFLUT;
	std::shared_ptr<PixelBuffer> m_pSSSROutput;
};