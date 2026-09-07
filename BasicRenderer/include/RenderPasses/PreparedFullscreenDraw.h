#pragma once

#include "Render/PreparedPass.h"
#include "Render/ShaderAPI.h"
#include "ShaderBuffers.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace br::render {

struct PreparedFullscreenDraw {
    rhi::DescriptorHeapHandle resourceHeap{};
    rhi::DescriptorHeapHandle samplerHeap{};
    rhi::DescriptorSlot renderTarget{};
    rhi::LoadOp loadOp = rhi::LoadOp::Load;
    decltype(rhi::ColorAttachment{}.clear) clear{};
    uint32_t width = 0;
    uint32_t height = 0;
    std::string debugName;
    rhi::PipelineLayoutHandle layout{};
    rhi::PipelineHandle pipeline{};
    std::shared_ptr<const void> pipelineOwner;
    std::vector<unsigned int> descriptorIndices;
    std::array<unsigned int, NumMiscUintRootConstants> constants{};
    rhi::ShaderStage constantStage = rhi::ShaderStage::Pixel;
};

inline void RecordPreparedFullscreenDraw(
    const PreparedFullscreenDraw& data, org::RecordingContext& recording) {
    auto& commands = recording.Commands();
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap);
    rhi::ColorAttachment color{};
    color.rtv = data.renderTarget;
    color.loadOp = data.loadOp;
    color.storeOp = rhi::StoreOp::Store;
    color.clear = data.clear;
    rhi::PassBeginInfo begin{};
    begin.colors = {&color, 1};
    begin.width = data.width;
    begin.height = data.height;
    begin.debugName = data.debugName.empty() ? nullptr : data.debugName.c_str();
    commands.BeginPass(begin);
    commands.SetPrimitiveTopology(rhi::PrimitiveTopology::TriangleStrip);
    commands.BindLayout(data.layout);
    commands.BindPipeline(data.pipeline);
    if (!data.descriptorIndices.empty()) {
        commands.PushConstants(rhi::ShaderStage::All, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(data.descriptorIndices.size()), data.descriptorIndices.data());
    }
    commands.PushConstants(data.constantStage, 0, MiscUintRootSignatureIndex, 0,
        NumMiscUintRootConstants, data.constants.data());
    commands.Draw(3, 1, 0, 0);
}

} // namespace br::render
