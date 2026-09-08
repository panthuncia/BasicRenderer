#pragma once

#include "RenderPasses/Base/RenderPass.h"
#include "Render/RenderContext.h"
#include "Managers/ViewManager.h"
#include <vector>

class LinearDepthHistoryCopyPass : public RenderPass {
public:
    explicit LinearDepthHistoryCopyPass(ViewManager* viewManager)
        : m_viewManager(viewManager) {
    }

    void DeclareResourceUsages(RenderPassBuilder* builder) override {
        // The current depth pyramid remains intact until the next frame's
        // phase-1 cull consumes it. Declaring the read keeps this marker after
        // the final phase-2 depth writes without copying the texture.
        builder->WithShaderResource(Builtin::LinearDepthMaps);
    }

    void Setup() override {
    }

    PassReturn Execute(PassExecutionContext& executionContext) override {
        if (!m_viewManager) {
            return {};
        }
        m_viewManager->ForEachView([&](uint64_t viewID) {
            const auto* view = m_viewManager->Get(viewID);
            if (view && view->gpu.linearDepthMap) {
                m_viewManager->MarkDepthHistoryValid(viewID);
            }
        });

        return {};
    }

    PreparedPass PrepareFrame(FramePreparationContext& preparation) override {
        (void)preparation;
        SubmissionEffect effect{};
        effect.viewManager = m_viewManager;
        if (m_viewManager) {
            m_viewManager->ForEachView([&](uint64_t viewID) {
                const auto* view = m_viewManager->Get(viewID);
                if (view && view->gpu.linearDepthMap) effect.viewIDs.push_back(viewID);
            });
        }
        return PreparedPass::MakeOwned(std::move(effect), &RecordNoOp, &CommitSubmitted);
    }

    void Cleanup() override {
    }

private:
    struct SubmissionEffect {
        ViewManager* viewManager = nullptr;
        std::vector<uint64_t> viewIDs;
    };
    static void RecordNoOp(const SubmissionEffect&, RecordingContext&) {}
    static void CommitSubmitted(const SubmissionEffect& effect) {
        if (!effect.viewManager) return;
        for (const auto viewID : effect.viewIDs) effect.viewManager->MarkDepthHistoryValid(viewID);
    }
    ViewManager* m_viewManager = nullptr;
};
