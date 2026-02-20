#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Render/RenderContext.h"
#include "Scene/Scene.h"
#include "Managers/Singletons/FFXManager.h"

class ScreenSpaceReflectionsPass : public ComputePass {
public:
    ScreenSpaceReflectionsPass() {
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) {
        builder->WithLegacyInterop(Builtin::Color::HDRColorTarget,
            Builtin::GBuffer::MotionVectors,
            Builtin::PrimaryCamera::DepthTexture,
            Builtin::GBuffer::Normals,
            Builtin::GBuffer::MetallicRoughness,
            Builtin::GBuffer::MotionVectors,
            Builtin::Environment::CurrentPrefilteredCubemap,
            Builtin::BRDFLUT,
            Builtin::PostProcessing::ScreenSpaceReflections);

        ResourceState outState{
        .access = rhi::ResourceAccessType::Common, 
        .layout = rhi::ResourceLayout::Common, 
        .sync = rhi::ResourceSyncState::All};

		ResourceIdentifierAndRange outResource(Builtin::PostProcessing::ScreenSpaceReflections, {});

        builder->WithInternalTransition(
            outResource,
            outState);
    }

    void Setup() override {
        m_pHDRTarget = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::Color::HDRColorTarget);
        m_pMotionVectors = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::GBuffer::MotionVectors);
        m_pDepthTexture = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PrimaryCamera::DepthTexture);
		m_pNormals = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::GBuffer::Normals);
        m_pMetallicRoughness = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::GBuffer::MetallicRoughness);
		m_pMotionVectors = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::GBuffer::MotionVectors);
		m_pEnvironmentCubemap = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::Environment::CurrentPrefilteredCubemap);
        m_pBRDFLUT = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::BRDFLUT);
		m_pSSSROutput = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PostProcessing::ScreenSpaceReflections);
    }

    PassReturn Execute(RenderContext& context) override {

		// Clear the render target of the SSSR output
        //context.commandList.ClearRenderTargetView( m_pSSSROutput->GetRTVInfo(0).slot, m_pSSSROutput->GetClearColor());

		context.commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

		// Transition SSSR output to UAV state
  //      CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
  //          m_pSSSROutput->GetAPIResource(),
  //          D3D12_RESOURCE_STATE_COMMON,
  //          D3D12_RESOURCE_STATE_UNORDERED_ACCESS
		//);
		//context.commandList->ResourceBarrier(1, &barrier);

        // Clear as UAV
  //      context.commandList.ClearUavFloat(
  //          { m_pSSSROutput->GetUAVShaderVisibleInfo(0).slot,
  //            m_pSSSROutput->GetUAVNonShaderVisibleInfo(0).slot,
  //            m_pSSSROutput->GetAPIResource() },
		//	m_pSSSROutput->GetClearColor().rgba
		//);

        FFXManager::GetInstance().EvaluateSSSR(
            context,
            m_pHDRTarget,
            m_pDepthTexture,
			m_pNormals,
			m_pMetallicRoughness,
			m_pMotionVectors,
            m_pEnvironmentCubemap,
			m_pBRDFLUT,
            m_pSSSROutput
		);

		// All resources must exit in COMMON state for legacy interop

		//barrier = CD3DX12_RESOURCE_BARRIER::Transition(
  //          m_pSSSROutput->GetAPIResource(),
  //          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
  //          D3D12_RESOURCE_STATE_COMMON
  //      );

		//context.commandList->ResourceBarrier(1, &barrier);

        return {};
    }

    void Cleanup() override {
        // Cleanup the render pass
    }

private:

    PixelBuffer* m_pHDRTarget;
    PixelBuffer* m_pMotionVectors;
    PixelBuffer* m_pDepthTexture;
	PixelBuffer* m_pNormals;
	PixelBuffer* m_pMetallicRoughness;
	PixelBuffer* m_pEnvironmentCubemap;
	PixelBuffer* m_pBRDFLUT;
	PixelBuffer* m_pSSSROutput;
};