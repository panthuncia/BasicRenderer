#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Utilities/Utilities.h"
#include "../shaders/PerPassRootConstants/luminanceHistogramRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

class LuminanceHistogramPass : public ComputePass {
public:
    LuminanceHistogramPass() {
        CreateComputePSO();
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) override {
        builder->WithShaderResource(Builtin::Color::HDRColorTarget)
            .WithUnorderedAccess(Builtin::PostProcessing::LuminanceHistogram);
    }

    void Setup() override {
		// Removed redundant Register calls now covered by declared-resource auto descriptor registration
    }

	PassReturn Execute(PassExecutionContext& executionContext) override {
        auto* renderContext = executionContext.hostData->Get<RenderContext>();
        auto& context = *renderContext;
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = executionContext.commandList;

		commandList.SetDescriptorHeaps(executionContext.GetResourceDescriptorHeap().GetHandle(),
			executionContext.GetSamplerDescriptorHeap().GetHandle());

        // Set the compute pipeline state
		commandList.BindLayout(psoManager.GetComputeRootSignature(executionContext.backendInstance).GetHandle());
		commandList.BindPipeline(psoManager.ResolvePipeline(m_pso, executionContext.backendInstance).GetHandle());

        uint32_t passConstants[NumMiscUintRootConstants] = {};
        passConstants[MIN_LOG_LUMINANCE] = as_uint(0.001f); // Minimum log luminance value
        passConstants[INVERSE_LOG_LUM_RANGE] = as_uint(1.0f / (log2(10.0f) - log2(0.1f))); // Inverse range for log luminance

        commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, passConstants);

		BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

        // Dispatch
        // In luminance histogram each thread group handles a 16x16 block
        const unsigned int sampledWidth = (context.renderResolution.x + 3) / 4;
        const unsigned int sampledHeight = (context.renderResolution.y + 3) / 4;
        unsigned int x = (sampledWidth + 16 - 1) / 16;
        unsigned int y = (sampledHeight + 16 - 1) / 16;
        commandList.Dispatch(x, y, 1);

        return {};
    }

    PreparedPass PrepareFrame(FramePreparationContext& preparation) override {
        const auto* context = preparation.preparationData->Get<UpdateContext>();
        auto payload = m_pso.GetPayload();
        br::render::PreparedComputeDispatch data{};
        data.resourceHeap = context->textureDescriptorHeap.GetHandle();
        data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
        data.layout = PSOManager::GetInstance().GetComputeRootSignature().GetHandle();
        data.pipeline = payload->pso.Get().GetHandle();
        data.pipelineOwner = std::move(payload);
        data.descriptorIndices = CaptureResourceDescriptorIndices(data.pipelineOwner->pipelineResources);
        data.constants[MIN_LOG_LUMINANCE] = as_uint(0.001f);
        data.constants[INVERSE_LOG_LUM_RANGE] = as_uint(1.0f / (log2(10.0f) - log2(0.1f)));
        const auto sampledWidth = (context->renderResolution.x + 3u) / 4u;
        const auto sampledHeight = (context->renderResolution.y + 3u) / 4u;
        data.groupsX = (sampledWidth + 15u) / 16u;
        data.groupsY = (sampledHeight + 15u) / 16u;
        return PreparedPass::Make(std::move(data), &br::render::RecordPreparedComputeDispatch);
    }

    void Cleanup() override {
        // Cleanup if necessary
    }

private:
    PipelineState m_pso;

    void CreateComputePSO()
    {
		m_pso = PSOManager::GetInstance().MakeComputePipeline(
			PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
			L"shaders/PostProcessing/LuminanceHistogram.hlsl",
			L"CSMain",
		    {}, 
            "LuminanceHistogramPassCS");
    }
};
