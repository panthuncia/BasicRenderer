#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <mutex>

#include "Render/PipelineState.h"
#include "RenderPasses/Base/ComputePass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class VirtualShadowMapAdmitPagesPass final : public ComputePass {
public:
    using AcquireUpgradeUploadFn =
        std::function<bool(uint32_t&, uint32_t&)>;
    using ReleaseUpgradeUploadFn = std::function<void(uint32_t)>;

    VirtualShadowMapAdmitPagesPass(
        std::shared_ptr<PixelBuffer> pageTableTexture,
        std::shared_ptr<Buffer> dirtyPageFlagsBuffer,
        std::vector<std::shared_ptr<Buffer>> upgradeInputBuffers,
        std::shared_ptr<Buffer> pageMetadataBuffer,
        std::shared_ptr<Buffer> clipmapInfoBuffer,
        std::shared_ptr<Buffer> compactShadowCamerasBuffer,
        std::shared_ptr<Buffer> statsBuffer,
        AcquireUpgradeUploadFn acquireUpgradeUpload,
        ReleaseUpgradeUploadFn releaseUpgradeUpload,
        uint32_t framesInFlight);

    void DeclareResourceUsages(ComputePassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    PreparedPass PrepareFrame(FramePreparationContext& preparation) override;
    void Cleanup() override;

private:
    struct UpgradeSubmissionState {
        std::mutex mutex;
        AcquireUpgradeUploadFn acquire;
        ReleaseUpgradeUploadFn release;
        std::vector<uint32_t> inFlightSlotByFrame;
        uint32_t pendingSlot = UINT32_MAX;
        uint32_t pendingCount = 0;
        uint64_t pendingGeneration = 0;
    };
    struct PreparedData {
        br::render::PreparedComputePipelineSequence commands;
        std::shared_ptr<UpgradeSubmissionState> state;
        uint32_t pendingSlot = UINT32_MAX;
        uint32_t pendingCount = 0;
        uint32_t frameSlot = 0;
        uint64_t pendingGeneration = 0;
    };
    static void RecordPrepared(const PreparedData&, RecordingContext&);
    static void CommitPrepared(const PreparedData&);
    PipelineState m_pso;
    PipelineState m_applyUpgradesPso;
    std::shared_ptr<PixelBuffer> m_pageTableTexture;
    std::shared_ptr<Buffer> m_dirtyPageFlagsBuffer;
    std::vector<std::shared_ptr<Buffer>> m_upgradeInputBuffers;
    std::shared_ptr<Buffer> m_pageMetadataBuffer;
    std::shared_ptr<Buffer> m_clipmapInfoBuffer;
    std::shared_ptr<Buffer> m_compactShadowCamerasBuffer;
    std::shared_ptr<Buffer> m_statsBuffer;
    std::shared_ptr<UpgradeSubmissionState> m_upgradeState;
};
