#include "Render/GraphExtensions/ClusterLOD/ClearDeepVisibilityPass.h"

#include <vector>

#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"

ClearDeepVisibilityPass::ClearDeepVisibilityPass(
    std::shared_ptr<Buffer> deepVisibilityCounterBuffer,
    std::shared_ptr<Buffer> deepVisibilityOverflowCounterBuffer,
    std::shared_ptr<Buffer> deepVisibilityStatsBuffer)
    : m_deepVisibilityCounterBuffer(std::move(deepVisibilityCounterBuffer))
    , m_deepVisibilityOverflowCounterBuffer(std::move(deepVisibilityOverflowCounterBuffer))
    , m_deepVisibilityStatsBuffer(std::move(deepVisibilityStatsBuffer)) {
}

void ClearDeepVisibilityPass::Declare(org::PassBuilder& declaration)
{
    auto* builder = &declaration;
    builder->WithUnorderedAccess(
        m_deepVisibilityCounterBuffer,
        m_deepVisibilityOverflowCounterBuffer,
        m_deepVisibilityStatsBuffer);
    for (auto& texture : m_headPointerTextures) {
        builder->WithUnorderedAccess(texture);
    }
}



void ClearDeepVisibilityPass::Update(const UpdateExecutionContext& executionContext)
{
    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;

    const uint32_t zero = 0u;
    if (m_deepVisibilityCounterBuffer) {
        BUFFER_UPLOAD(&zero, sizeof(uint32_t), org::runtime::UploadTarget::FromShared(m_deepVisibilityCounterBuffer), 0);
    }
    if (m_deepVisibilityOverflowCounterBuffer) {
        BUFFER_UPLOAD(&zero, sizeof(uint32_t), org::runtime::UploadTarget::FromShared(m_deepVisibilityOverflowCounterBuffer), 0);
    }
    if (m_deepVisibilityStatsBuffer) {
        const CLodDeepVisibilityStats zeroStats{};
        BUFFER_UPLOAD(&zeroStats, sizeof(CLodDeepVisibilityStats), org::runtime::UploadTarget::FromShared(m_deepVisibilityStatsBuffer), 0);
    }

    std::vector<std::shared_ptr<PixelBuffer>> headPointerTextures;
    context.viewManager->ForEachView([&](uint64_t viewID) {
        auto* view = context.viewManager->Get(viewID);
        if (!view || !view->gpu.visibilityBuffer) {
            return;
        }

        auto headPointers = context.viewManager->EnsureCLodDeepVisibilityHeadPointers(viewID);
        if (headPointers) {
            headPointerTextures.push_back(std::move(headPointers));
        }
    });

    m_declaredResourcesChanged = m_headPointerTextures != headPointerTextures;
    m_headPointerTextures = std::move(headPointerTextures);
}

bool ClearDeepVisibilityPass::DeclaredResourcesChanged() const
{
    return m_declaredResourcesChanged;
}

br::render::PreparedResourceClears ClearDeepVisibilityPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto& context = *preparation.preparationData->Get<UpdateContext>();
    br::render::PreparedResourceClears data{};
    data.resourceHeap = context.textureDescriptorHeap.GetHandle();
    data.samplerHeap = context.samplerDescriptorHeap.GetHandle();
    for (const auto& texture : m_headPointerTextures) {
        const org::ResourceBindingToken binding{texture->GetGlobalResourceID(), 0};
        data.clears.push_back({preparation.CaptureResource(binding),
            preparation.CaptureDescriptor(binding, texture->GetUAVNonShaderVisibleInfo(0).slot),
            preparation.CaptureDescriptor(binding, texture->GetUAVShaderVisibleInfo(0).slot),
            0.0f, 0xFFFFFFFFu, false});
    }
    return data;
}

void ClearDeepVisibilityPass::Record(const br::render::PreparedResourceClears& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedResourceClears(data, recording);
}
