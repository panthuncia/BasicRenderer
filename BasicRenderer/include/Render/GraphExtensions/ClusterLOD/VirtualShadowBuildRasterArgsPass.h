#pragma once

#include <memory>
#include <vector>

#include "BuiltinResources.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"
#include "../../../../shaders/PerPassRootConstants/clodVirtualShadowBuildArgsRootConstants.h"

namespace org { class Buffer; }
using org::Buffer;

class VirtualShadowBuildRasterArgsPass : public org::TypedRenderGraphPass<VirtualShadowBuildRasterArgsPass, br::render::PreparedComputeDispatch> {
public:
    VirtualShadowBuildRasterArgsPass(
        std::shared_ptr<Buffer> histogramBuffer,
        std::shared_ptr<Buffer> offsetsBuffer,
        std::shared_ptr<Buffer> indirectArgsBuffer,
        bool runWhenComputeSWRasterEnabledOnly = false)
        : m_histogramBuffer(std::move(histogramBuffer))
        , m_offsetsBuffer(std::move(offsetsBuffer))
        , m_indirectArgsBuffer(std::move(indirectArgsBuffer))
        , m_runWhenComputeSWRasterEnabledOnly(runWhenComputeSWRasterEnabledOnly)
    {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"Shaders/ClusterLOD/virtualShadowBlockExpansion.hlsl",
            L"CLodVirtualShadowBuildRasterArgsCSMain",
            {},
            "CLod_VirtualShadowBuildRasterArgsPSO");
    }

    void Declare(org::PassBuilder& declaration) {
        declaration.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
        auto* builder = &declaration;
        builder->WithShaderResource(m_histogramBuffer, m_offsetsBuffer)
            .WithUnorderedAccess(m_indirectArgsBuffer)
            .WithConstantBuffer(Builtin::PerFrameBuffer);
    }

    void Update(const UpdateExecutionContext& executionContext) override
    {
        if (m_runWhenComputeSWRasterEnabledOnly &&
            !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
            return;
        }

        auto* updateContext = executionContext.hostData->Get<UpdateContext>();
        auto& context = *updateContext;
        const uint32_t numBuckets = context.preparedRasterBucketCount;
        if (m_indirectArgsBuffer->GetSize() < static_cast<size_t>(numBuckets) * sizeof(RasterizeClustersCommand)) {
            m_indirectArgsBuffer->ResizeStructured(numBuckets);
        }
    }

    br::render::PreparedComputeDispatch Prepare(const org::PassPrepareContext& preparation) {

        if (m_runWhenComputeSWRasterEnabledOnly &&
            !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
            return {};
        }

        const auto& context = *preparation.preparationData->Get<UpdateContext>();
        br::render::PreparedComputeDispatch data{};
        const uint32_t numBuckets = context.preparedRasterBucketCount;
        if (numBuckets == 0u) return {};
        data.resourceHeap = context.textureDescriptorHeap.GetHandle();
        data.samplerHeap = context.samplerDescriptorHeap.GetHandle();
        auto program = preparation.CaptureProgramBinding(m_pso);
        data.program = program.program;
        data.descriptorIndices = std::move(program.descriptorIndices);
        data.constants[CLOD_VSM_BUILD_ARGS_HISTOGRAM_DESCRIPTOR_INDEX] = m_histogramBuffer->GetSRVInfo(0).slot.index;
        data.constants[CLOD_VSM_BUILD_ARGS_OFFSETS_DESCRIPTOR_INDEX] = m_offsetsBuffer->GetSRVInfo(0).slot.index;
        data.constants[CLOD_VSM_BUILD_ARGS_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_indirectArgsBuffer->GetUAVShaderVisibleInfo(0).slot.index;
        data.constants[CLOD_VSM_BUILD_ARGS_NUM_BUCKETS] = numBuckets;
        data.groupsX = (numBuckets + 63u) / 64u;
        return data;
    }

    static void Record(const br::render::PreparedComputeDispatch& data, org::PassRecordContext& recording) {
        br::render::RecordPreparedComputeDispatch(data, recording);
    }

private:
    PipelineState m_pso;
    std::shared_ptr<Buffer> m_histogramBuffer;
    std::shared_ptr<Buffer> m_offsetsBuffer;
    std::shared_ptr<Buffer> m_indirectArgsBuffer;
    bool m_runWhenComputeSWRasterEnabledOnly = false;
};
