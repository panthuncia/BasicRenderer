#pragma once

#include "RenderPasses/Base/ComputePass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "../shaders/PerPassRootConstants/luminanceHistogramAverageRootConstants.h"

class LuminanceHistogramAveragePass : public ComputePass {
public:
    LuminanceHistogramAveragePass() {
        CreateComputePSO();
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) {
        builder->WithUnorderedAccess(Builtin::PostProcessing::LuminanceHistogram, Builtin::PostProcessing::AdaptedLuminance, "FFX::LPMConstants");
    }

    void Setup() override {
		RegisterUAV(Builtin::PostProcessing::AdaptedLuminance);
        RegisterUAV(Builtin::PostProcessing::LuminanceHistogram);
		RegisterSRV("FFX::LPMConstants");
    }

    PassReturn Execute(RenderContext& context) override {
        auto& psoManager = PSOManager::GetInstance();
        auto& commandList = context.commandList;

		commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

        // Set the compute pipeline state
		commandList.BindLayout(psoManager.GetComputeRootSignature().GetHandle());
		commandList.BindPipeline(m_pso.GetAPIPipelineState().GetHandle());

        float passConstants[NumMiscFloatRootConstants] = {};
        passConstants[MIN_LOG_LUMINANCE] = 0.001f; // Minimum log luminance value
        passConstants[LOG_LUMINANCE_RANGE] = (log2(10.0f) - log2(0.1f)); // range for log luminance
        passConstants[TIME_COEFFICIENT] = context.deltaTime;
		passConstants[NUM_PIXELS] = static_cast<float>(context.renderResolution.x * context.renderResolution.y);

        //commandList->SetComputeRoot32BitConstants(MiscFloatRootSignatureIndex, NumMiscFloatRootConstants, passConstants, 0);
		commandList.PushConstants(rhi::ShaderStage::Compute, 0, MiscFloatRootSignatureIndex, 0, NumMiscFloatRootConstants, passConstants);

        BindResourceDescriptorIndices(commandList, m_pso.GetResourceDescriptorSlots());

		// Dispatch the compute shader
        commandList.Dispatch(1, 1, 1);

        return {};
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
