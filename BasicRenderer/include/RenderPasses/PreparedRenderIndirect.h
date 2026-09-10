#pragma once

#include "Render/PreparedPass.h"
#include "Render/PipelineState.h"
#include "Render/ShaderAPI.h"
#include "ShaderBuffers.h"
#include <BasicTelemetry/Telemetry.h>

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
    bool phase1VisibilityDiagnostics = false;
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
    if (data.phase1VisibilityDiagnostics) {
        const auto arguments = recording.Resolve(data.arguments).GetHandle();
        const auto firstPipeline = recording.Resolve(data.steps.front().program.program);
        basic_telemetry::SetGauge("BasicRenderer.CLod.Phase1.RecordedSteps",
            static_cast<int64_t>(data.steps.size()));
        basic_telemetry::SetGauge("BasicRenderer.CLod.Phase1.RecordedArgumentResourceIndex",
            static_cast<int64_t>(arguments.index));
        basic_telemetry::SetGauge("BasicRenderer.CLod.Phase1.RecordedArgumentResourceGeneration",
            static_cast<int64_t>(arguments.generation));
        basic_telemetry::SetGauge("BasicRenderer.CLod.Phase1.RecordedFirstPipelineIndex",
            static_cast<int64_t>(firstPipeline.index));
        basic_telemetry::SetGauge("BasicRenderer.CLod.Phase1.RecordedFirstPipelineGeneration",
            static_cast<int64_t>(firstPipeline.generation));
    }
    rhi::PassBeginInfo pass{};
    pass.colors = {data.colors.data(), data.colorCount};
    pass.depth = data.hasDepth ? &data.depth : nullptr;
    pass.width = data.width; pass.height = data.height; pass.debugName = data.debugName;
    commands.BeginPass(pass);
    // The recording envelope binds the accepted frame's shared bindless heaps
    // before any pass records. Do not replace them with preparation-time handles:
    // delayed frames can otherwise record against a newer heap publication.
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
