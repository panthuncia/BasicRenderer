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
        org::PreparedProgramBinding program{};
        uint64_t argumentsOffset = 0;
    };
    rhi::DescriptorHeapHandle resourceHeap{}, samplerHeap{};
    rhi::CommandSignatureHandle commandSignature{};
    org::PreparedResourceReference arguments{};
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
    // Preparation may legitimately produce no draws (for example an empty
    // culling result). Match the synchronous pass contract: graph-owned
    // barriers still execute around this packet, but the packet itself emits
    // no render-pass or root-signature commands.
    if (data.steps.empty()) return;
    auto& commands = recording.Commands();
    rhi::PassBeginInfo pass{};
    pass.colors = {data.colors.data(), data.colorCount};
    pass.depth = data.hasDepth ? &data.depth : nullptr;
    pass.width = data.width; pass.height = data.height; pass.debugName = data.debugName;
    commands.BeginPass(pass);
    // Admission binds the execution-slot descriptor snapshot. Preparation-time
    // handles are optional legacy data and must never replace it with an invalid
    // handle (for example when the frame request was captured before admission).
    if (data.resourceHeap.valid())
        commands.SetDescriptorHeaps(data.resourceHeap,
            data.samplerHeap.valid() ? std::optional{data.samplerHeap} : std::nullopt);
    commands.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleList);
    for (const auto& step : data.steps) {
        commands.BindLayout(recording.ResolveLayout(step.program.program));
        commands.BindPipeline(recording.Resolve(step.program.program));
        // Root arguments belong to the bound layout. A fresh recording list has
        // no layout, and switching layouts can invalidate the previous arguments.
        commands.PushConstants(rhi::ShaderStage::AllGraphics, 0, MiscUintRootSignatureIndex, 0,
            NumMiscUintRootConstants, data.constants.data());
        if (!step.program.descriptorIndices.empty()) commands.PushConstants(rhi::ShaderStage::AllGraphics, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(step.program.descriptorIndices.size()), step.program.descriptorIndices.data());
		const auto arguments = recording.Resolve(data.arguments).GetHandle();
        commands.ExecuteIndirect(data.commandSignature, arguments, step.argumentsOffset, {}, 0, 1);
    }
    commands.EndPass();
}

} // namespace br::render
