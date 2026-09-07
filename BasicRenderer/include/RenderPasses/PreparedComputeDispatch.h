#pragma once

#include "Render/PreparedPass.h"
#include "Render/PipelineState.h"
#include "Render/ShaderAPI.h"
#include "ShaderBuffers.h"

#include <array>
#include <memory>
#include <vector>

namespace br::render {

// Immutable recording packet for the common one-dispatch compute-pass shape.
// Passes capture their frame-varying constants during PrepareFrame; the packet
// owns the pipeline payload and contains no pointer back to the mutable pass.
struct PreparedComputeDispatch {
    rhi::DescriptorHeapHandle resourceHeap{};
    rhi::DescriptorHeapHandle samplerHeap{};
    rhi::PipelineLayoutHandle layout{};
    rhi::PipelineHandle pipeline{};
    std::shared_ptr<const org::PipelineStatePayload> pipelineOwner;
    std::vector<unsigned int> descriptorIndices;
    std::array<unsigned int, NumMiscUintRootConstants> constants{};
    uint32_t groupsX = 0;
    uint32_t groupsY = 1;
    uint32_t groupsZ = 1;
};

inline void RecordPreparedComputeDispatch(
    const PreparedComputeDispatch& data, org::RecordingContext& recording) {
    auto& commands = recording.Commands();
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap);
    commands.BindLayout(data.layout);
    commands.BindPipeline(data.pipeline);
    if (!data.descriptorIndices.empty()) {
        commands.PushConstants(rhi::ShaderStage::Compute, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(data.descriptorIndices.size()), data.descriptorIndices.data());
    }
    commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
        NumMiscUintRootConstants, data.constants.data());
    if (data.groupsX != 0 && data.groupsY != 0 && data.groupsZ != 0)
        commands.Dispatch(data.groupsX, data.groupsY, data.groupsZ);
}

struct PreparedComputeIndirect {
    bool enabled = true;
    rhi::DescriptorHeapHandle resourceHeap{};
    rhi::DescriptorHeapHandle samplerHeap{};
    rhi::PipelineLayoutHandle layout{};
    rhi::PipelineHandle pipeline{};
    std::shared_ptr<const org::PipelineStatePayload> pipelineOwner;
    std::shared_ptr<const void> commandSignatureOwner;
    rhi::CommandSignatureHandle commandSignature{};
    rhi::ResourceHandle arguments{}, countBuffer{};
    std::vector<unsigned int> descriptorIndices;
    std::array<unsigned int, NumMiscUintRootConstants> constants{};
    uint64_t argumentsOffset = 0, countOffset = 0;
    uint32_t maximumCount = 1;
};

inline void RecordPreparedComputeIndirect(
    const PreparedComputeIndirect& data, org::RecordingContext& recording) {
    if (!data.enabled) return;
    auto& commands = recording.Commands();
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap);
    commands.BindLayout(data.layout);
    commands.BindPipeline(data.pipeline);
    if (!data.descriptorIndices.empty()) {
        commands.PushConstants(rhi::ShaderStage::Compute, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(data.descriptorIndices.size()), data.descriptorIndices.data());
    }
    commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
        NumMiscUintRootConstants, data.constants.data());
    commands.ExecuteIndirect(data.commandSignature, data.arguments, data.argumentsOffset,
        data.countBuffer, data.countOffset, data.maximumCount);
}

struct PreparedComputeIndirectSequence {
    struct Step {
        rhi::PipelineHandle pipeline{};
        std::shared_ptr<const org::PipelineStatePayload> pipelineOwner;
        std::vector<unsigned int> descriptorIndices;
        std::array<unsigned int, NumMiscUintRootConstants> constants{};
        uint64_t argumentsOffset = 0;
        uint32_t maximumCount = 1;
    };
    rhi::DescriptorHeapHandle resourceHeap{}, samplerHeap{};
    rhi::PipelineLayoutHandle layout{};
    rhi::CommandSignatureHandle commandSignature{};
    std::shared_ptr<const void> commandSignatureOwner;
    rhi::ResourceHandle arguments{}, countBuffer{};
    std::shared_ptr<const void> argumentsOwner;
    uint64_t countOffset = 0;
    std::vector<Step> steps;
};

inline void RecordPreparedComputeIndirectSequence(
    const PreparedComputeIndirectSequence& data, org::RecordingContext& recording) {
    auto& commands = recording.Commands();
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap);
    commands.BindLayout(data.layout);
    for (const auto& step : data.steps) {
        commands.BindPipeline(step.pipeline);
        if (!step.descriptorIndices.empty()) commands.PushConstants(rhi::ShaderStage::Compute, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(step.descriptorIndices.size()), step.descriptorIndices.data());
        commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
            NumMiscUintRootConstants, step.constants.data());
        commands.ExecuteIndirect(data.commandSignature, data.arguments, step.argumentsOffset,
            data.countBuffer, data.countOffset, step.maximumCount);
    }
}

struct PreparedComputeDispatchSequence {
    struct Step {
        std::array<unsigned int, NumMiscUintRootConstants> constants{};
        uint32_t groupsX = 0, groupsY = 1, groupsZ = 1;
        bool uavBarrierBefore = false;
        bool uavBarrierAfter = false;
    };
    rhi::DescriptorHeapHandle resourceHeap{};
    rhi::DescriptorHeapHandle samplerHeap{};
    rhi::PipelineLayoutHandle layout{};
    rhi::PipelineHandle pipeline{};
    std::shared_ptr<const org::PipelineStatePayload> pipelineOwner;
    std::vector<unsigned int> descriptorIndices;
    std::vector<Step> steps;
};

inline void RecordPreparedComputeDispatchSequence(
    const PreparedComputeDispatchSequence& data, org::RecordingContext& recording) {
    auto& commands = recording.Commands();
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap);
    commands.BindLayout(data.layout);
    commands.BindPipeline(data.pipeline);
    if (!data.descriptorIndices.empty()) commands.PushConstants(rhi::ShaderStage::Compute, 0,
        org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
        static_cast<uint32_t>(data.descriptorIndices.size()), data.descriptorIndices.data());
    for (const auto& step : data.steps) {
        if (step.uavBarrierBefore) {
            rhi::GlobalBarrier barrier{};
            barrier.beforeSync = barrier.afterSync = rhi::ResourceSyncState::ComputeShading;
            barrier.beforeAccess = barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            rhi::BarrierBatch barriers{}; barriers.globals = {&barrier, 1}; commands.Barriers(barriers);
        }
        commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
            NumMiscUintRootConstants, step.constants.data());
        commands.Dispatch(step.groupsX, step.groupsY, step.groupsZ);
        if (step.uavBarrierAfter) {
            rhi::GlobalBarrier barrier{};
            barrier.beforeSync = barrier.afterSync = rhi::ResourceSyncState::ComputeShading;
            barrier.beforeAccess = barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            rhi::BarrierBatch barriers{}; barriers.globals = {&barrier, 1}; commands.Barriers(barriers);
        }
    }
}

// Sequence variant for passes which switch immutable compute pipelines between
// dispatches. Each step owns its pipeline payload so recording never reaches
// back into the mutable pass or PSO manager.
struct PreparedComputePipelineSequence {
    struct Step {
        rhi::PipelineHandle pipeline{};
        std::shared_ptr<const org::PipelineStatePayload> pipelineOwner;
        std::vector<unsigned int> descriptorIndices;
        std::array<unsigned int, NumMiscUintRootConstants> constants{};
        uint32_t groupsX = 0, groupsY = 1, groupsZ = 1;
        bool uavBarrierBefore = false;
        bool uavBarrierAfter = false;
    };
    rhi::DescriptorHeapHandle resourceHeap{};
    rhi::DescriptorHeapHandle samplerHeap{};
    rhi::PipelineLayoutHandle layout{};
    std::vector<Step> steps;
};

inline void RecordPreparedComputePipelineSequence(
    const PreparedComputePipelineSequence& data, org::RecordingContext& recording) {
    auto& commands = recording.Commands();
    commands.SetDescriptorHeaps(data.resourceHeap, data.samplerHeap);
    commands.BindLayout(data.layout);
    for (const auto& step : data.steps) {
        if (step.uavBarrierBefore) {
            rhi::GlobalBarrier barrier{};
            barrier.beforeSync = barrier.afterSync = rhi::ResourceSyncState::ComputeShading;
            barrier.beforeAccess = barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            rhi::BarrierBatch barriers{};
            barriers.globals = {&barrier, 1};
            commands.Barriers(barriers);
        }
        commands.BindPipeline(step.pipeline);
        if (!step.descriptorIndices.empty()) commands.PushConstants(rhi::ShaderStage::Compute, 0,
            org::shaderapi::kResourceDescriptorIndicesRootParameter, 0,
            static_cast<uint32_t>(step.descriptorIndices.size()), step.descriptorIndices.data());
        commands.PushConstants(rhi::ShaderStage::Compute, 0, MiscUintRootSignatureIndex, 0,
            NumMiscUintRootConstants, step.constants.data());
        if (step.groupsX != 0 && step.groupsY != 0 && step.groupsZ != 0)
            commands.Dispatch(step.groupsX, step.groupsY, step.groupsZ);
        if (step.uavBarrierAfter) {
            rhi::GlobalBarrier barrier{};
            barrier.beforeSync = barrier.afterSync = rhi::ResourceSyncState::ComputeShading;
            barrier.beforeAccess = barrier.afterAccess = rhi::ResourceAccessType::UnorderedAccess;
            rhi::BarrierBatch barriers{};
            barriers.globals = {&barrier, 1};
            commands.Barriers(barriers);
        }
    }
}

} // namespace br::render
