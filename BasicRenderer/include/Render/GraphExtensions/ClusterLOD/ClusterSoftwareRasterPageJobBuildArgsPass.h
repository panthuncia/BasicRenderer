#pragma once

#include <array>
#include <memory>

#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"
#include "Resources/Buffers/Buffer.h"
#include "../../../../shaders/PerPassRootConstants/clodRasterizationRootConstants.h"

class ClusterSoftwareRasterPageJobBuildArgsPass : public org::TypedRenderGraphPass<ClusterSoftwareRasterPageJobBuildArgsPass, br::render::PreparedComputeDispatchSequence> {
public:
    ClusterSoftwareRasterPageJobBuildArgsPass(
        std::shared_ptr<Buffer> rigidPageJobCountBuffer,
        std::shared_ptr<Buffer> rigidPageJobIndirectArgsBuffer,
        std::shared_ptr<Buffer> skinnedPageJobCountBuffer,
        std::shared_ptr<Buffer> skinnedPageJobIndirectArgsBuffer,
        bool runWhenComputeSWRasterEnabledOnly = false)
        : m_pageJobCountBuffers{ std::move(rigidPageJobCountBuffer), std::move(skinnedPageJobCountBuffer) }
        , m_pageJobIndirectArgsBuffers{ std::move(rigidPageJobIndirectArgsBuffer), std::move(skinnedPageJobIndirectArgsBuffer) }
        , m_runWhenComputeSWRasterEnabledOnly(runWhenComputeSWRasterEnabledOnly)
    {
        m_pso = PSOManager::GetInstance().MakeComputePipeline(
            PSOManager::GetInstance().GetComputeRootSignature().GetHandle(),
            L"Shaders/ClusterLOD/softwareRasterPageJobs.hlsl",
            L"SWPageJobBuildIndirectArgsCSMain",
            {},
            "CLod_SoftwarePageJobBuildIndirectArgsPSO");
    }

    void Declare(org::PassBuilder& declaration) {
        declaration.PreferQueue(org::QueueKind::Compute).AutomaticQueueAssignment();
        auto* builder = &declaration;
        builder->WithShaderResource(m_pageJobCountBuffers[0], m_pageJobCountBuffers[1])
            .WithUnorderedAccess(m_pageJobIndirectArgsBuffers[0], m_pageJobIndirectArgsBuffers[1]);
    }

    br::render::PreparedComputeDispatchSequence Prepare(const org::PassPrepareContext& preparation) {

        if (m_runWhenComputeSWRasterEnabledOnly &&
            !CLodSoftwareRasterUsesCompute(SettingsManager::GetInstance().getSettingGetter<CLodSoftwareRasterMode>(CLodSoftwareRasterModeSettingName)())) {
            return {};
        }
        if (!CLodVSMRasterModeUsesLargeClusterPageJob(
                SettingsManager::GetInstance().getSettingGetter<CLodVSMRasterMode>(CLodVSMRasterModeSettingName)())) {
            return {};
        }

        const auto& context = *preparation.preparationData->Get<UpdateContext>();
        br::render::PreparedComputeDispatchSequence data{};
        data.resourceHeap = context.textureDescriptorHeap.GetHandle();
        data.samplerHeap = context.samplerDescriptorHeap.GetHandle();
        auto program = preparation.CaptureProgramBinding(m_pso);
        data.program = program.program;
        data.descriptorIndices = std::move(program.descriptorIndices);
        for (uint32_t variantIndex = 0; variantIndex < m_pageJobCountBuffers.size(); ++variantIndex) {
            br::render::PreparedComputeDispatchSequence::Step step{};
            step.constants[CLOD_RASTER_PAGE_JOB_COUNT_DESCRIPTOR_INDEX] = m_pageJobCountBuffers[variantIndex]->GetSRVInfo(0).slot.index;
            step.constants[CLOD_RASTER_PAGE_JOB_INDIRECT_ARGS_DESCRIPTOR_INDEX] = m_pageJobIndirectArgsBuffers[variantIndex]->GetUAVShaderVisibleInfo(0).slot.index;
            step.groupsX = 1;
            data.steps.push_back(std::move(step));
        }
        return data;
    }

    static void Record(const br::render::PreparedComputeDispatchSequence& data, org::PassRecordContext& recording) {
        br::render::RecordPreparedComputeDispatchSequence(data, recording);
    }

private:
    PipelineState m_pso;
    std::array<std::shared_ptr<Buffer>, 2> m_pageJobCountBuffers;
    std::array<std::shared_ptr<Buffer>, 2> m_pageJobIndirectArgsBuffers;
    bool m_runWhenComputeSWRasterEnabledOnly = false;
};
