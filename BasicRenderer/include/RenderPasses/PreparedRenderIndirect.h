#pragma once

#include "Render/PreparedPass.h"
#include "Render/PipelineState.h"
#include "Render/ShaderAPI.h"
#include "ShaderBuffers.h"

#include <array>
#include <memory>
#include <vector>

namespace br::render {

struct PreparedRenderIndirectSequence {
    struct Step {
        rhi::PipelineHandle pipeline{};
        std::shared_ptr<const org::PipelineStatePayload> pipelineOwner;
        std::vector<unsigned int> descriptorIndices;
        uint64_t argumentsOffset = 0;
    };
    rhi::DescriptorHeapHandle resourceHeap{}, samplerHeap{};
    rhi::PipelineLayoutHandle layout{};
    rhi::CommandSignatureHandle commandSignature{};
    rhi::ResourceHandle arguments{};
    std::shared_ptr<const void> argumentsOwner;
    std::array<unsigned int, NumMiscUintRootConstants> constants{};
    std::array<rhi::ColorAttachment, 3> colors{};
    uint32_t colorCount = 0;
    rhi::DepthAttachment depth{};
    bool hasDepth = false;
    uint32_t width = 1, height = 1;
    const char* debugName = nullptr;
    std::vector<Step> steps;
};

inline void RecordPreparedRenderIndirectSequence(
    const PreparedRenderIndirectSequence& data, org::RecordingContext& recording) {
    auto& commands = recording.Commands();
    rhi::PassBeginInfo pass{};
    pass.colors = {data.colors.data(), data.colorCount};
    pass.depth = data.hasDepth ? &data.depth : nullptr;
    pass.width = data.width; pass.height = data.height; pass.debugName = data.debugName;
    commands.BeginPass(pass);
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap);
    commands.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
    commands.BindLayout(data.layout);
    commands.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0,
        NumMiscUintRootConstants, data.constants.data());
    for (const auto& step : data.steps) {
        commands.BindPipeline(step.pipeline);
        if (!step.descriptorIndices.empty()) commands.PushConstants(rhi::ShaderStage::AllGraphics, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(step.descriptorIndices.size()), step.descriptorIndices.data());
        commands.ExecuteIndirect(data.commandSignature, data.arguments, step.argumentsOffset, {}, 0, 1);
    }
}

} // namespace br::render
