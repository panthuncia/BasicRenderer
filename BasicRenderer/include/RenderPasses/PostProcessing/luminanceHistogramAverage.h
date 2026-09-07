#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Utilities/Utilities.h"
#include "../shaders/PerPassRootConstants/luminanceHistogramAverageRootConstants.h"
#include "RenderPasses/PreparedComputeDispatch.h"

class LuminanceHistogramAveragePass : public ComputePass {
public:
    LuminanceHistogramAveragePass() {
        CreateComputePSO();
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) override {
        builder->WithUnorderedAccess(Builtin::PostProcessing::LuminanceHistogram, Builtin::PostProcessing::AdaptedLuminance, "FFX::LPMConstants");
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
        passConstants[LOG_LUMINANCE_RANGE] = as_uint(log2(10.0f) - log2(0.1f)); // range for log luminance
        passConstants[TIME_COEFFICIENT] = as_uint(context.deltaTime);
		passConstants[NUM_PIXELS] = as_uint(static_cast<float>(context.renderResolution.x * context.renderResolution.y));

		commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0, NumMiscUintRootConstants, passConstants);

        BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

		// Dispatch the compute shader
        commandList.Dispatch(1, 1, 1);

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
        data.constants[LOG_LUMINANCE_RANGE] = as_uint(log2(10.0f) - log2(0.1f));
        data.constants[TIME_COEFFICIENT] = as_uint(context->deltaTime);
        data.constants[NUM_PIXELS] = as_uint(static_cast<float>(
            context->renderResolution.x * context->renderResolution.y));
        data.groupsX = 1;
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
			L"shaders/PostProcessing/LuminanceHistogramAverage.hlsl",
			L"CSMain",
			{},
			"LuminanceHistogramAverageCS");
    }
};
