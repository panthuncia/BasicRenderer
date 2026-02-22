#include "Resources/PixelBuffer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "Managers/Singletons/DescriptorHeapManager.h"
#include "Resources/GPUBacking/GPUTextureBacking.h"
#include "Utilities/ORGUtilities.h"

namespace {
DescriptorHeapManager::ViewRequirements::TextureViews BuildTextureViewRequirements(
    const TextureDescription& desc,
    uint32_t mipLevels,
    uint32_t totalArraySlices)
{
    DescriptorHeapManager::ViewRequirements::TextureViews texViews;
    texViews.mipLevels = mipLevels;
    texViews.isCubemap = desc.isCubemap;
    texViews.isArray = desc.isArray;
    texViews.arraySize = desc.arraySize;
    texViews.totalArraySlices = totalArraySlices;

    texViews.baseFormat = desc.format;
    texViews.srvFormat = desc.srvFormat;
    texViews.uavFormat = desc.uavFormat;
    texViews.rtvFormat = desc.rtvFormat;
    texViews.dsvFormat = desc.dsvFormat;

    texViews.createSRV = true;
    texViews.createUAV = desc.hasUAV;
    texViews.createNonShaderVisibleUAV = desc.hasNonShaderVisibleUAV;
    texViews.createRTV = desc.hasRTV;
    texViews.createDSV = desc.hasDSV;

    if (desc.hasUAV && rhi::helpers::IsSRGB(desc.format)) {
        if (texViews.srvFormat == rhi::Format::Unknown) {
            texViews.srvFormat = desc.format;
        }
        texViews.baseFormat = rhi::helpers::typlessFromSrgb(desc.format);
        texViews.uavFormat = rhi::helpers::stripSrgb(desc.format);
    }

    texViews.createCubemapAsArraySRV = desc.isCubemap;
    texViews.uavFirstMip = 0;
    return texViews;
}
}

PixelBuffer::PixelBuffer(const TextureDescription& desc, bool materialize)
{
    m_hasLayout = true;
    m_desc = desc;
    if (materialize) {
        Materialize();
    }
    if (desc.padInternalResolution) {
        m_internalWidth = (std::max)(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(desc.imageDimensions[0].width)))));
        m_internalHeight = (std::max)(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(desc.imageDimensions[0].height)))));
    }
    else {
        m_internalHeight = desc.imageDimensions[0].height;
        m_internalWidth = desc.imageDimensions[0].width;
    }

    m_clearValue.type = rhi::ClearValueType::Color;
    m_clearValue.format = desc.format;
    m_clearValue.depthStencil.depth = desc.depthClearValue;
    if (desc.hasDSV) {
        m_clearValue.type = rhi::ClearValueType::DepthStencil;
    }
    else {
        for (int i = 0; i < 4; i++) {
            m_clearValue.rgba[i] = desc.clearColor[i];
        }
    }
}

PixelBuffer::~PixelBuffer() = default;

rhi::Resource PixelBuffer::GetAPIResource() {
    EnsureMaterialized("GetAPIResource");
    return m_backing->GetAPIResource();
}

rhi::BarrierBatch PixelBuffer::GetEnhancedBarrierGroup(
    RangeSpec range,
    rhi::ResourceAccessType prevAccessType,
    rhi::ResourceAccessType newAccessType,
    rhi::ResourceLayout prevLayout,
    rhi::ResourceLayout newLayout,
    rhi::ResourceSyncState prevSyncState,
    rhi::ResourceSyncState newSyncState)
{
    EnsureMaterialized("GetEnhancedBarrierGroup");
    return m_backing->GetEnhancedBarrierGroup(
        range, prevAccessType, newAccessType, prevLayout, newLayout, prevSyncState, newSyncState
    );
}

void PixelBuffer::OnSetName() {
    if (m_backing) {
        m_backing->SetName(name.c_str());
    }
}

void PixelBuffer::ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) const {
    if (!m_backing) {
        return;
    }
    m_backing->ApplyMetadataComponentBundle(bundle);
}

SymbolicTracker* PixelBuffer::GetStateTracker() {
    EnsureMaterialized("GetStateTracker");
    return m_backing->GetStateTracker();
}

void PixelBuffer::Materialize(const MaterializeOptions* options) {
    if (m_backing) {
        return;
    }

    EnsureVirtualDescriptorSlotsAllocated();

    auto newDesc = m_desc;
    if (m_desc.padInternalResolution) {
        for (auto& dim : m_desc.imageDimensions) {
            dim.width = (std::max)(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(dim.width)))));
            dim.height = (std::max)(1u, static_cast<unsigned int>(std::pow(2, std::ceil(std::log2(dim.height)))));
        }
    }

    if (options && options->aliasPlacement.has_value()) {
        m_backing = GpuTextureBacking::CreateUnique(newDesc, GetGlobalResourceID(), options->aliasPlacement.value(), name.empty() ? nullptr : name.c_str());
    }
    else {
        m_backing = GpuTextureBacking::CreateUnique(newDesc, GetGlobalResourceID(), name.empty() ? nullptr : name.c_str());
    }

    m_mipLevels = m_backing->GetMipLevels();
    m_arraySize = m_backing->GetArraySize();

    auto& rm = DescriptorHeapManager::GetInstance();
    DescriptorHeapManager::ViewRequirements views;
    views.views = BuildTextureViewRequirements(m_desc, m_mipLevels, m_arraySize);
    auto res = m_backing->GetAPIResource();
    rm.UpdateDescriptorContents(*this, res, views);

    if (m_desc.aliasingPoolID.has_value()) {
        m_backing->ApplyMetadataComponentBundle(
            EntityComponentBundle().Set<MemoryStatisticsComponents::AliasingPool>({ m_desc.aliasingPoolID })
        );
    }

    ++m_backingGeneration;
}

void PixelBuffer::Dematerialize() {
    if (!m_backing) {
        return;
    }

    m_backing.reset();
    ++m_backingGeneration;
}

void PixelBuffer::EnsureVirtualDescriptorSlotsAllocated() {
    if (HasAnyDescriptorSlots()) {
        return;
    }

    auto& rm = DescriptorHeapManager::GetInstance();
    const uint16_t mipLevels = m_desc.generateMipMaps
        ? rg::util::CalculateMipLevels(m_desc.imageDimensions[0].width, m_desc.imageDimensions[0].height)
        : 1;
    const uint32_t arraySize = m_desc.isCubemap
        ? 6u * m_desc.arraySize
        : (m_desc.isArray ? m_desc.arraySize : 1u);

    DescriptorHeapManager::ViewRequirements views;
    views.views = BuildTextureViewRequirements(m_desc, mipLevels, arraySize);
    rm.ReserveDescriptorSlots(*this, views);
}

void PixelBuffer::EnsureMaterialized(const char* operation) const {
    if (m_backing) {
        return;
    }
    throw std::runtime_error(std::string("PixelBuffer '") + name + "' is unmaterialized during " + operation);
}

void PixelBuffer::ApplyMetadataComponentBundle(const EntityComponentBundle& bundle) {
    if (m_backing) {
        m_backing->ApplyMetadataComponentBundle(bundle);
    }
}
