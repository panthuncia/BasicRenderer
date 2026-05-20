#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Render/RenderContext.h"
#include "Resources/PixelBuffer.h"

class RayTracedReflectionsPass : public ComputePass {
public:
    void DeclareResourceUsages(ComputePassBuilder* builder) override {
        builder->WithShaderResource(
            Builtin::Color::HDRColorTarget,
            Builtin::PrimaryCamera::DepthTexture,
            Builtin::GBuffer::Normals,
            Builtin::GBuffer::MetallicRoughness,
            Builtin::CameraBuffer,
            Builtin::Environment::CurrentPrefilteredCubemap);
        builder->WithUnorderedAccess(Builtin::PostProcessing::ScreenSpaceReflections);

        ResourceState outState{
            .access = rhi::ResourceAccessType::Common,
            .layout = rhi::ResourceLayout::Common,
            .sync = rhi::ResourceSyncState::All,
        };

        builder->WithInternalTransition(
            ResourceIdentifierAndRange(Builtin::PostProcessing::ScreenSpaceReflections, {}),
            outState);
    }

    void Setup() override {
        m_output = m_resourceRegistryView->RequestPtr<PixelBuffer>(Builtin::PostProcessing::ScreenSpaceReflections);
    }

    PassReturn Execute(PassExecutionContext& executionContext) override {
        auto* renderContext = executionContext.hostData->Get<RenderContext>();
        if (!renderContext || !m_output) {
            return {};
        }

        auto& commandList = executionContext.commandList;
        commandList.SetDescriptorHeaps(
            renderContext->textureDescriptorHeap.GetHandle(),
            renderContext->samplerDescriptorHeap.GetHandle());

        rhi::UavClearInfo clearInfo{};
        clearInfo.cpuVisible = m_output->GetUAVNonShaderVisibleInfo(0).slot;
        clearInfo.shaderVisible = m_output->GetUAVShaderVisibleInfo(0).slot;
        clearInfo.resource = m_output->GetAPIResource();

        rhi::UavClearFloat clearValue{};
        clearValue.v[0] = 0.0f;
        clearValue.v[1] = 0.0f;
        clearValue.v[2] = 0.0f;
        clearValue.v[3] = 0.0f;
        commandList.ClearUavFloat(clearInfo, clearValue);
        return {};
    }

    void Cleanup() override {}

private:
    PixelBuffer* m_output = nullptr;
};