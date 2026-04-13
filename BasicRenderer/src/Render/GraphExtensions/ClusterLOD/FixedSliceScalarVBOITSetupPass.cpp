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
    std::shared_ptr<PixelBuffer> integratedTransmittanceTexture,
    std::shared_ptr<PixelBuffer> zeroTransmittanceSliceTexture,
    std::shared_ptr<PixelBuffer> accumulationTexture)
    : m_configBuffer(std::move(configBuffer))
    , m_occupancyTexture(std::move(occupancyTexture))
    , m_extinctionTexture(std::move(extinctionTexture))
    , m_integratedTransmittanceTexture(std::move(integratedTransmittanceTexture))
    , m_zeroTransmittanceSliceTexture(std::move(zeroTransmittanceSliceTexture))
    , m_accumulationTexture(std::move(accumulationTexture))
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
        m_integratedTransmittanceTexture,
        m_zeroTransmittanceSliceTexture);

    if (m_accumulationTexture) {
        builder->WithRenderTarget(m_accumulationTexture);
    }
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
    m_zeroTransmittanceSliceTexture->EnsureVirtualDescriptorSlotsAllocated();

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
    config.zeroTransmittanceSliceUAVDescriptorIndex = m_zeroTransmittanceSliceTexture
        ? m_zeroTransmittanceSliceTexture->GetUAVShaderVisibleInfo(0).slot.index
        : 0xFFFFFFFFu;
    config.zeroTransmittanceSliceSRVDescriptorIndex = m_zeroTransmittanceSliceTexture
        ? m_zeroTransmittanceSliceTexture->GetSRVInfo(0).slot.index
        : 0xFFFFFFFFu;
    config.sliceCount = CLodFixedSliceScalarVBOITDefaultSliceCount;
    config.lowResolutionWidth = m_occupancyTexture ? m_occupancyTexture->GetWidth() : 0u;
    config.lowResolutionHeight = m_occupancyTexture ? m_occupancyTexture->GetHeight() : 0u;
    config.depthDistributionExponent = CLodFixedSliceScalarVBOITDefaultDepthDistributionExponent;
    config.lookupDepthBiasInSlices = CLodFixedSliceScalarVBOITDefaultLookupDepthBiasInSlices;
    config.zeroTransmittanceThreshold = CLodFixedSliceScalarVBOITDefaultZeroTransmittanceThreshold;

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

    const auto clearFloatResource = [&commandList](PixelBuffer* resource, float clearValueScalar = 0.0f) {
        if (!resource) {
            return;
        }

        rhi::UavClearFloat clearValue{};
        clearValue.v[0] = clearValueScalar;
        clearValue.v[1] = clearValueScalar;
        clearValue.v[2] = clearValueScalar;
        clearValue.v[3] = clearValueScalar;

        const unsigned int sliceCount = resource->GetNumUAVSlices();
        for (unsigned int sliceIndex = 0; sliceIndex < sliceCount; ++sliceIndex) {
            rhi::UavClearInfo clearInfo{};
            clearInfo.cpuVisible = resource->GetUAVNonShaderVisibleInfo(0, sliceIndex).slot;
            clearInfo.shaderVisible = resource->GetUAVShaderVisibleInfo(0, sliceIndex).slot;
            clearInfo.resource = resource->GetAPIResource();
            commandList.ClearUavFloat(clearInfo, clearValue);
        }
    };

    const auto clearUintResource = [&commandList](PixelBuffer* resource, uint32_t clearValueScalar = 0u) {
        if (!resource) {
            return;
        }

        rhi::UavClearUint clearValue{};
        clearValue.v[0] = clearValueScalar;
        clearValue.v[1] = clearValueScalar;
        clearValue.v[2] = clearValueScalar;
        clearValue.v[3] = clearValueScalar;

        const unsigned int sliceCount = resource->GetNumUAVSlices();
        for (unsigned int sliceIndex = 0; sliceIndex < sliceCount; ++sliceIndex) {
            rhi::UavClearInfo clearInfo{};
            clearInfo.cpuVisible = resource->GetUAVNonShaderVisibleInfo(0, sliceIndex).slot;
            clearInfo.shaderVisible = resource->GetUAVShaderVisibleInfo(0, sliceIndex).slot;
            clearInfo.resource = resource->GetAPIResource();
            commandList.ClearUavUint(clearInfo, clearValue);
        }
    };

    clearFloatResource(m_occupancyTexture.get());
    if (m_extinctionTexture && m_extinctionTexture->GetFormat() == rhi::Format::R32_UInt) {
        clearUintResource(m_extinctionTexture.get());
    }
    else {
        clearFloatResource(m_extinctionTexture.get());
    }
    clearFloatResource(m_integratedTransmittanceTexture.get(), 1.0f);
    if (m_zeroTransmittanceSliceTexture) {
        clearUintResource(m_zeroTransmittanceSliceTexture.get(), CLodFixedSliceScalarVBOITDefaultSliceCount);
    }

    if (m_accumulationTexture) {
        commandList.ClearRenderTargetView(
            m_accumulationTexture->GetRTVInfo(0).slot,
            m_accumulationTexture->GetClearColor());
    }
    return {};
}

void FixedSliceScalarVBOITSetupPass::Cleanup()
{
}