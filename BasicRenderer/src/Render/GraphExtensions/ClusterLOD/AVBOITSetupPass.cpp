#include "Render/GraphExtensions/ClusterLOD/AVBOITSetupPass.h"

#include "Managers/ViewManager.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/RenderContext.h"
#include "Render/Runtime/UploadServiceAccess.h"
#include "Resources/Buffers/Buffer.h"
#include "Resources/PixelBuffer.h"
#include "Resources/GloballyIndexedResource.h"

AVBOITSetupPass::AVBOITSetupPass(
    std::shared_ptr<Buffer> configBuffer,
    std::shared_ptr<Buffer> fitStateBuffer,
    std::shared_ptr<Buffer> depthWarpLUTBuffer,
    std::shared_ptr<PixelBuffer> occupancyTexture,
    std::shared_ptr<PixelBuffer> coverageTexture,
    std::shared_ptr<PixelBuffer> occupancySliceMaskTexture,
    std::shared_ptr<PixelBuffer> scalarExtinctionTexture,
    std::shared_ptr<PixelBuffer> chromaticExtinctionTexture,
    std::shared_ptr<PixelBuffer> integratedTransmittanceTexture,
    std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture,
    std::shared_ptr<PixelBuffer> accumulationTexture,
    std::shared_ptr<PixelBuffer> normalizationTexture,
    std::shared_ptr<PixelBuffer> shadingExtinctionTexture)
    : m_configBuffer(std::move(configBuffer))
    , m_fitStateBuffer(std::move(fitStateBuffer))
    , m_depthWarpLUTBuffer(std::move(depthWarpLUTBuffer))
    , m_occupancyTexture(std::move(occupancyTexture))
    , m_coverageTexture(std::move(coverageTexture))
    , m_occupancySliceMaskTexture(std::move(occupancySliceMaskTexture))
    , m_scalarExtinctionTexture(std::move(scalarExtinctionTexture))
    , m_chromaticExtinctionTexture(std::move(chromaticExtinctionTexture))
    , m_integratedTransmittanceTexture(std::move(integratedTransmittanceTexture))
    , m_zeroTransmittanceSliceTexture(std::move(zeroTransmittanceSliceTexture))
    , m_accumulationTexture(std::move(accumulationTexture))
    , m_normalizationTexture(std::move(normalizationTexture))
    , m_shadingExtinctionTexture(std::move(shadingExtinctionTexture))
{
}

void AVBOITSetupPass::Declare(org::PassBuilder& builder)
{
    if (m_configBuffer) {
        builder.WithShaderResource(m_configBuffer);
    }
    if (m_fitStateBuffer) {
        builder.WithShaderResource(m_fitStateBuffer);
    }
    if (m_depthWarpLUTBuffer) {
        builder.WithShaderResource(m_depthWarpLUTBuffer);
    }

    builder.WithUnorderedAccessClear(
        m_occupancyTexture,
        m_coverageTexture,
        m_occupancySliceMaskTexture,
        m_integratedTransmittanceTexture,
        m_zeroTransmittanceSliceTexture);

    builder.WithUnorderedAccess(
        m_scalarExtinctionTexture,
        m_chromaticExtinctionTexture);

    if (m_accumulationTexture) {
        builder.WithRenderTargetClear(m_accumulationTexture);
    }
    if (m_normalizationTexture) {
        builder.WithRenderTargetClear(m_normalizationTexture);
    }
    if (m_shadingExtinctionTexture) {
        builder.WithRenderTargetClear(m_shadingExtinctionTexture);
    }
}

void AVBOITSetupPass::Update(const UpdateExecutionContext& executionContext)
{
    if (!m_configBuffer) {
        return;
    }

    auto* updateContext = executionContext.hostData->Get<UpdateContext>();
    auto& context = *updateContext;

    m_configBuffer->EnsureVirtualDescriptorSlotsAllocated();
    m_occupancyTexture->EnsureVirtualDescriptorSlotsAllocated();
	m_coverageTexture->EnsureVirtualDescriptorSlotsAllocated();
    m_occupancySliceMaskTexture->EnsureVirtualDescriptorSlotsAllocated();
    if (m_fitStateBuffer) {
        m_fitStateBuffer->EnsureVirtualDescriptorSlotsAllocated();
    }
    if (m_depthWarpLUTBuffer) {
        m_depthWarpLUTBuffer->EnsureVirtualDescriptorSlotsAllocated();
    }
    m_scalarExtinctionTexture->EnsureVirtualDescriptorSlotsAllocated();
    m_chromaticExtinctionTexture->EnsureVirtualDescriptorSlotsAllocated();
	m_integratedTransmittanceTexture->EnsureVirtualDescriptorSlotsAllocated();
    m_zeroTransmittanceSliceTexture->EnsureVirtualDescriptorSlotsAllocated();

    CLodAVBOITConfig config{};
    config.occupancyUAVDescriptorIndex = m_occupancyTexture
        ? m_occupancyTexture->GetUAVShaderVisibleInfo(0).slot.index
        : 0xFFFFFFFFu;
    config.coverageUAVDescriptorIndex = m_coverageTexture
        ? m_coverageTexture->GetUAVShaderVisibleInfo(0).slot.index
        : 0xFFFFFFFFu;
    config.occupancySliceMaskUAVDescriptorIndex = m_occupancySliceMaskTexture
        ? m_occupancySliceMaskTexture->GetUAVShaderVisibleInfo(0).slot.index
        : 0xFFFFFFFFu;
    config.depthWarpLUTSRVDescriptorIndex = m_depthWarpLUTBuffer
        ? m_depthWarpLUTBuffer->GetSRVInfo(0).slot.index
        : 0xFFFFFFFFu;
    config.scalarExtinctionUAVDescriptorIndex = m_scalarExtinctionTexture
        ? m_scalarExtinctionTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index
        : 0xFFFFFFFFu;
    config.chromaticExtinctionUAVDescriptorIndex = m_chromaticExtinctionTexture
        ? m_chromaticExtinctionTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index
        : 0xFFFFFFFFu;
    config.integratedTransmittanceUAVDescriptorIndex = m_integratedTransmittanceTexture
        ? m_integratedTransmittanceTexture->GetUAVShaderVisibleInfo(UAVViewType::Texture2DArrayFull, 0).slot.index
        : 0xFFFFFFFFu;
    config.shadingTransmittanceSRVDescriptorIndex = m_integratedTransmittanceTexture
        ? m_integratedTransmittanceTexture->GetSRVInfo(SRVViewType::Texture2DArrayFull, 0).slot.index
        : 0xFFFFFFFFu;
    config.zeroTransmittanceSliceUAVDescriptorIndex = m_zeroTransmittanceSliceTexture
        ? m_zeroTransmittanceSliceTexture->GetUAVShaderVisibleInfo(0).slot.index
        : 0xFFFFFFFFu;
    config.sliceCount = CLodAVBOITDefaultSliceCount;
    config.virtualSliceCount = CLodAVBOITDefaultVirtualSliceCount;
    config.lowResolutionWidth = m_occupancyTexture ? m_occupancyTexture->GetWidth() : 0u;
    config.lowResolutionHeight = m_occupancyTexture ? m_occupancyTexture->GetHeight() : 0u;
    config.depthDistributionExponent = CLodAVBOITDefaultDepthDistributionExponent;
    config.lookupDepthBiasInSlices = CLodAVBOITDefaultLookupDepthBiasInSlices;
    config.zeroTransmittanceThreshold = CLodAVBOITDefaultZeroTransmittanceThreshold;

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

    BUFFER_UPLOAD(&config, sizeof(CLodAVBOITConfig), org::runtime::UploadTarget::FromShared(m_configBuffer), 0);

    if (m_fitStateBuffer && !m_fitStateInitialized) {
        const CLodAVBOITFitState fitState{};
        BUFFER_UPLOAD(&fitState, sizeof(CLodAVBOITFitState), org::runtime::UploadTarget::FromShared(m_fitStateBuffer), 0);
        m_fitStateInitialized = true;
    }
}

AVBOITSetupFrameData AVBOITSetupPass::Prepare(const org::PassPrepareContext& preparation) {
    const auto* context = preparation.preparationData->Get<UpdateContext>();
    AVBOITSetupFrameData data;
    data.resourceHeap = context->textureDescriptorHeap.GetHandle();
    data.samplerHeap = context->samplerDescriptorHeap.GetHandle();
    const auto append = [&](PixelBuffer* resource, bool isFloat, float floatValue, uint32_t uintValue) {
        if (!resource) return;
        const org::ResourceBindingToken binding{resource->GetGlobalResourceID(), 0};
        const auto captured = preparation.CaptureResource(binding);
        for (uint32_t slice = 0; slice < resource->GetNumUAVSlices(); ++slice) {
            data.clears.push_back({captured,
                preparation.CaptureDescriptor(binding, resource->GetUAVNonShaderVisibleInfo(0, slice).slot),
                preparation.CaptureDescriptor(binding, resource->GetUAVShaderVisibleInfo(0, slice).slot),
                floatValue, uintValue, isFloat});
        }
    };
    append(m_occupancyTexture.get(), true, 0, 0);
    append(m_coverageTexture.get(), true, 0, 0);
    append(m_integratedTransmittanceTexture.get(), true, 1, 0);
    append(m_occupancySliceMaskTexture.get(), false, 0, 0);
    append(m_zeroTransmittanceSliceTexture.get(), false, 0, CLodAVBOITDefaultSliceCount);
    for (const auto* resource : {m_accumulationTexture.get(), m_normalizationTexture.get(), m_shadingExtinctionTexture.get()}) {
        if (resource) data.targets.push_back({preparation.CaptureDescriptor(
            {resource->GetGlobalResourceID(), 0}, resource->GetRTVInfo(0).slot), resource->GetClearColor()});
    }
    return data;
}

void AVBOITSetupPass::Record(const AVBOITSetupFrameData& data, org::PassRecordContext& recording) {
    br::render::RecordPreparedResourceClears(data, recording);
}
