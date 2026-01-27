#pragma once

#include <rhi_debug.h>

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/ViewManager.h"
#include <boost/container_hash/hash.hpp>


class RasterBucketHistogramPass : public ComputePass {
public:
    RasterBucketHistogramPass() {
        CreatePipelines(
            DeviceManager::GetInstance().GetDevice(),
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            m_histogramPipeline);

        // Used by the cluster rasterization pass
        rhi::IndirectArg rasterizeClustersArgs[] = {
            {.kind = rhi::IndirectArgKind::Constant, .u = {.rootConstants = { MiscUintRootSignatureIndex, 0, 2 } } },
            {.kind = rhi::IndirectArgKind::DispatchMesh }
        };

		auto device = DeviceManager::GetInstance().GetDevice();

        auto result = device.CreateCommandSignature(
            rhi::CommandSignatureDesc{ rhi::Span<rhi::IndirectArg>(rasterizeClustersArgs, 2), sizeof(RasterBucketHistogramIndirectCommand) },
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(), m_histogramCommandSignature);
    }

    ~RasterBucketHistogramPass() {
    }

    void DeclareResourceUsages(ComputePassBuilder* builder) {
        builder->WithShaderResource(
            Builtin::VisibleClusterBuffer,
            Builtin::VisibleClusterCounter)
            .WithIndirectArguments("Builtin::CLod::RasterBucketsHistogramIndirectCommand");
    }

    void Setup() override {

        RegisterSRV(Builtin::VisibleClusterBuffer);
        RegisterSRV(Builtin::VisibleClusterCounter);

        m_rasterBucketHistogramIndirectCommandsResource = m_resourceRegistryView->RequestPtr<Resource>("Builtin::CLod::RasterBucketsHistogramIndirectCommand");
    }

    PassReturn Execute(RenderContext& context) override {
        auto& commandList = context.commandList;

        // Set the descriptor heaps
        commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

        commandList.BindLayout(PSOManager::GetInstance().GetComputeRootSignature().GetHandle());

        commandList.BindPipeline(m_histogramPipeline.GetAPIPipelineState().GetHandle());
		BindResourceDescriptorIndices(commandList, m_histogramPipeline.GetResourceDescriptorSlots());

        // Single-dispatch ExecuteIndirect
        commandList.ExecuteIndirect(m_histogramCommandSignature->GetHandle(), m_rasterBucketHistogramIndirectCommandsResource->GetAPIResource().GetHandle(), 0, {}, 0, 1);
        
        return {};
    }

    void Update(const UpdateContext& context) override {
    }

    void Cleanup() override {

    }

private:
    PipelineState m_histogramPipeline;
    rhi::CommandSignaturePtr m_histogramCommandSignature;
	Resource* m_rasterBucketHistogramIndirectCommandsResource = nullptr;

    rhi::Result CreatePipelines(
        rhi::Device device,
        rhi::PipelineLayoutHandle globalRootSignature,
        PipelineState& outHistogramPipeline)
    {
        outHistogramPipeline = PSOManager::GetInstance().MakeComputePipeline(
            globalRootSignature,
            L"Shaders/ClusterLOD/RasterBucketHistogramCS.hlsl",
			L"RasterBucketHistogramCSMain");
    }
};