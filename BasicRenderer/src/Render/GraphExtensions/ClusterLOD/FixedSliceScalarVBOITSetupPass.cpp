#include "Render/GraphExtensions/ClusterLOD/FixedSliceScalarVBOITSetupPass.h"

#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "Resources/GloballyIndexedResource.h"

FixedSliceScalarVBOITSetupPass::FixedSliceScalarVBOITSetupPass(
    std::shared_ptr<Buffer> configBuffer,
    std::shared_ptr<PixelBuffer> occupancyTexture,
    std::shared_ptr<PixelBuffer> extinctionTexture,
    std::shared_ptr<PixelBuffer> integratedTransmittanceTexture)
    : m_configBuffer(std::move(configBuffer))
    , m_occupancyTexture(std::move(occupancyTexture))
    , m_extinctionTexture(std::move(extinctionTexture))
    , m_integratedTransmittanceTexture(std::move(integratedTransmittanceTexture))
{
}

void FixedSliceScalarVBOITSetupPass::DeclareResourceUsages(RenderPassBuilder* builder)
{
    if (m_configBuffer) {
        builder->WithShaderResource(m_configBuffer);
    }

    builder->WithUnorderedAccess(
        m_occupancyTexture,
        m_extinctionTexture,
        m_integratedTransmittanceTexture);
}

void FixedSliceScalarVBOITSetupPass::Setup()
{
}

void FixedSliceScalarVBOITSetupPass::Update(const UpdateExecutionContext& executionContext)
{
    if (!m_configBuffer) {
        return;
    }

    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;

    m_occupancyTexture->EnsureVirtualDescriptorSlotsAllocated();
	m_extinctionTexture->EnsureVirtualDescriptorSlotsAllocated();
	m_integratedTransmittanceTexture->EnsureVirtualDescriptorSlotsAllocated();

    CLodFixedSliceScalarVBOITConfig config{};
    config.occupancyUAVDescriptorIndex = m_occupancyTexture
        ? m_occupancyTexture->GetUAVShaderVisibleInfo(0).slot.index
        : 0xFFFFFFFFu;
    config.extinctionUAVDescriptorIndex = m_extinctionTexture
        ? m_extinctionTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index
        : 0xFFFFFFFFu;
    config.integratedTransmittanceUAVDescriptorIndex = m_integratedTransmittanceTexture
        ? m_integratedTransmittanceTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index
        : 0xFFFFFFFFu;
    config.shadingTransmittanceSRVDescriptorIndex = m_integratedTransmittanceTexture
        ? m_integratedTransmittanceTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index
        : 0xFFFFFFFFu;
    config.sliceCount = CLodFixedSliceScalarVBOITDefaultSliceCount;
    config.lowResolutionWidth = m_occupancyTexture ? m_occupancyTexture->GetWidth() : 0u;
    config.lowResolutionHeight = m_occupancyTexture ? m_occupancyTexture->GetHeight() : 0u;
    config.inverseSliceCount = config.sliceCount > 0u ? 1.0f / static_cast<float>(config.sliceCount) : 0.0f;
    config.lowResolutionScale = context.renderResolution.x > 0u
        ? static_cast<float>(config.lowResolutionWidth) / static_cast<float>(context.renderResolution.x)
        : CLodFixedSliceScalarVBOITDefaultResolutionScale;

    if (context.viewManager) {
        context.viewManager->ForEachFiltered(ViewFilter::PrimaryCameras(), [&](uint64_t viewID) {
            const View* view = context.viewManager->Get(viewID);
            if (!view) {
                return;
            }

            config.viewNearDepth = view->cameraInfo.zNear;
            config.viewFarDepth = view->cameraInfo.zFar;
        });
    }

    BUFFER_UPLOAD(&config, sizeof(CLodFixedSliceScalarVBOITConfig), rg::runtime::UploadTarget::FromShared(m_configBuffer), 0);
}

PassReturn FixedSliceScalarVBOITSetupPass::Execute(PassExecutionContext& executionContext)
{
    auto* renderContext = executionContext.hostData->Get<RenderContext>();
    auto& context = *renderContext;
    auto& commandList = executionContext.commandList;

    commandList.SetDescriptorHeaps(context.textureDescriptorHeap.GetHandle(), context.samplerDescriptorHeap.GetHandle());

    const auto clearFloatResource = [&commandList](PixelBuffer* resource) {
        if (!resource) {
            return;
        }

        rhi::UavClearInfo clearInfo{};
        clearInfo.cpuVisible = resource->GetUAVNonShaderVisibleInfo(0).slot;
        clearInfo.shaderVisible = resource->GetUAVShaderVisibleInfo(0).slot;
        clearInfo.resource = resource->GetAPIResource();

        rhi::UavClearFloat clearValue{};
        clearValue.v[0] = 0.0f;
        clearValue.v[1] = 0.0f;
        clearValue.v[2] = 0.0f;
        clearValue.v[3] = 0.0f;
        commandList.ClearUavFloat(clearInfo, clearValue);
    };

    clearFloatResource(m_occupancyTexture.get());
    clearFloatResource(m_extinctionTexture.get());
    clearFloatResource(m_integratedTransmittanceTexture.get());
    return {};
}

void FixedSliceScalarVBOITSetupPass::Cleanup()
{
}