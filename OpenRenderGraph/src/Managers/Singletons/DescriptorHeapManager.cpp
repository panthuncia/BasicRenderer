#include "Managers/Singletons/DescriptorHeapManager.h"

#include <stdexcept>

#include <spdlog/spdlog.h>
#include <rhi_helpers.h>

#include "Managers/Singletons/DeviceManager.h"
#include "Resources/GloballyIndexedResource.h"

void DescriptorHeapManager::Initialize() {
    auto device = DeviceManager::GetInstance().GetDevice();

    m_cbvSrvUavHeap = std::make_shared<DescriptorHeap>(
        device,
        rhi::DescriptorHeapType::CbvSrvUav,
        1000000,
        true,
        "cbvSrvUavHeap");

    m_samplerHeap = std::make_shared<DescriptorHeap>(
        device,
        rhi::DescriptorHeapType::Sampler,
        2048,
        true,
        "samplerHeap");

    m_rtvHeap = std::make_shared<DescriptorHeap>(
        device,
        rhi::DescriptorHeapType::RTV,
        10000,
        false,
        "rtvHeap");

    m_dsvHeap = std::make_shared<DescriptorHeap>(
        device,
        rhi::DescriptorHeapType::DSV,
        10000,
        false,
        "dsvHeap");

    m_nonShaderVisibleHeap = std::make_shared<DescriptorHeap>(
        device,
        rhi::DescriptorHeapType::CbvSrvUav,
        100000,
        false,
        "nonShaderVisibleHeap");
}

void DescriptorHeapManager::Cleanup() {
    m_cbvSrvUavHeap.reset();
    m_samplerHeap.reset();
    m_rtvHeap.reset();
    m_dsvHeap.reset();
    m_nonShaderVisibleHeap.reset();
}

void DescriptorHeapManager::AssignDescriptorSlots(
    GloballyIndexedResource& target,
    rhi::Resource& apiResource,
    const ViewRequirements& req)
{
    ReserveDescriptorSlots(target, req);
    UpdateDescriptorContents(target, apiResource, req);
}

void DescriptorHeapManager::ReserveDescriptorSlots(
    GloballyIndexedResource& target,
    const ViewRequirements& req)
{
    if (target.HasAnyDescriptorSlots()) {
        return;
    }

    if (!m_cbvSrvUavHeap || !m_samplerHeap || !m_rtvHeap || !m_dsvHeap || !m_nonShaderVisibleHeap) {
        spdlog::error("DescriptorHeapManager::ReserveDescriptorSlots called before DescriptorHeapManager::Initialize");
        throw std::runtime_error("DescriptorHeapManager::ReserveDescriptorSlots called before DescriptorHeapManager::Initialize");
    }

    if (const auto* tex = std::get_if<ViewRequirements::TextureViews>(&req.views)) {
        auto makeShaderVisibleGrid = [&](uint32_t slices, uint32_t mips, const std::shared_ptr<DescriptorHeap>& heap) {
            std::vector<std::vector<ShaderVisibleIndexInfo>> infos;
            infos.resize(slices);
            for (uint32_t slice = 0; slice < slices; ++slice) {
                infos[slice].resize(mips);
                for (uint32_t mip = 0; mip < mips; ++mip) {
                    ShaderVisibleIndexInfo info{};
                    info.slot.index = heap->AllocateDescriptor();
                    info.slot.heap = heap->GetHeap().GetHandle();
                    infos[slice][mip] = info;
                }
            }
            return infos;
        };

        auto makeNonShaderVisibleGrid = [&](uint32_t slices, uint32_t mips, const std::shared_ptr<DescriptorHeap>& heap) {
            std::vector<std::vector<NonShaderVisibleIndexInfo>> infos;
            infos.resize(slices);
            for (uint32_t slice = 0; slice < slices; ++slice) {
                infos[slice].resize(mips);
                for (uint32_t mip = 0; mip < mips; ++mip) {
                    NonShaderVisibleIndexInfo info{};
                    info.slot.index = heap->AllocateDescriptor();
                    info.slot.heap = heap->GetHeap().GetHandle();
                    infos[slice][mip] = info;
                }
            }
            return infos;
        };

        const uint32_t srvSlices = (tex->isArray || tex->isCubemap) ? tex->arraySize : 1u;
        const uint32_t uavSlices = (tex->isArray || tex->isCubemap) ? tex->totalArraySlices : 1u;

        SRVViewType srvViewType = SRVViewType::Invalid;
        if (tex->isArray) {
            srvViewType = tex->isCubemap ? SRVViewType::TextureCubeArray : SRVViewType::Texture2DArray;
        }
        else if (tex->isCubemap) {
            srvViewType = SRVViewType::TextureCube;
        }
        else {
            srvViewType = SRVViewType::Texture2D;
        }

        if (tex->createSRV) {
            target.SetDefaultSRVViewType(srvViewType);
            target.SetSRVView(srvViewType, m_cbvSrvUavHeap, makeShaderVisibleGrid(srvSlices, tex->mipLevels, m_cbvSrvUavHeap));

            if (tex->createCubemapAsArraySRV && tex->isCubemap) {
                target.SetSRVView(SRVViewType::Texture2DArray, m_cbvSrvUavHeap, makeShaderVisibleGrid(6u, tex->mipLevels, m_cbvSrvUavHeap));
            }
        }

        if (tex->createUAV) {
            target.SetUAVGPUDescriptors(m_cbvSrvUavHeap, makeShaderVisibleGrid(uavSlices, tex->mipLevels, m_cbvSrvUavHeap));
        }

        if (tex->createNonShaderVisibleUAV) {
            target.SetUAVCPUDescriptors(m_nonShaderVisibleHeap, makeNonShaderVisibleGrid(uavSlices, tex->mipLevels, m_nonShaderVisibleHeap));
        }

        if (tex->createRTV) {
            target.SetRTVDescriptors(m_rtvHeap, makeNonShaderVisibleGrid(uavSlices, tex->mipLevels, m_rtvHeap));
        }

        if (tex->createDSV) {
            target.SetDSVDescriptors(m_dsvHeap, makeNonShaderVisibleGrid(uavSlices, tex->mipLevels, m_dsvHeap));
        }

        return;
    }

    if (const auto* buf = std::get_if<ViewRequirements::BufferViews>(&req.views)) {
        if (buf->createCBV) {
            ShaderVisibleIndexInfo cbvInfo{};
            cbvInfo.slot.index = m_cbvSrvUavHeap->AllocateDescriptor();
            cbvInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();
            target.SetCBVDescriptor(m_cbvSrvUavHeap, cbvInfo);
        }

        if (buf->createSRV) {
            ShaderVisibleIndexInfo srvInfo{};
            srvInfo.slot.index = m_cbvSrvUavHeap->AllocateDescriptor();
            srvInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();
            target.SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, { { srvInfo } });
        }

        if (buf->createUAV) {
            ShaderVisibleIndexInfo uavInfo{};
            uavInfo.slot.index = m_cbvSrvUavHeap->AllocateDescriptor();
            uavInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();
            target.SetUAVGPUDescriptors(m_cbvSrvUavHeap, { { uavInfo } }, buf->uavCounterOffset);
        }

        if (buf->createNonShaderVisibleUAV) {
            NonShaderVisibleIndexInfo uavInfo{};
            uavInfo.slot.index = m_nonShaderVisibleHeap->AllocateDescriptor();
            uavInfo.slot.heap = m_nonShaderVisibleHeap->GetHeap().GetHandle();
            target.SetUAVCPUDescriptors(m_nonShaderVisibleHeap, { { uavInfo } });
        }

        return;
    }

    spdlog::error("DescriptorHeapManager::ReserveDescriptorSlots: invalid ViewRequirements variant");
    throw std::runtime_error("DescriptorHeapManager::ReserveDescriptorSlots: invalid ViewRequirements");
}

void DescriptorHeapManager::UpdateDescriptorContents(
    GloballyIndexedResource& target,
    rhi::Resource& apiResource,
    const ViewRequirements& req)
{
    auto device = DeviceManager::GetInstance().GetDevice();

    if (!m_cbvSrvUavHeap || !m_samplerHeap || !m_rtvHeap || !m_dsvHeap || !m_nonShaderVisibleHeap) {
        spdlog::error("DescriptorHeapManager::UpdateDescriptorContents called before DescriptorHeapManager::Initialize");
        throw std::runtime_error("DescriptorHeapManager::UpdateDescriptorContents called before DescriptorHeapManager::Initialize");
    }

    if (const auto* tex = std::get_if<ViewRequirements::TextureViews>(&req.views)) {
        if (tex->createSRV) {
            SRVViewType srvViewType = SRVViewType::Invalid;
            if (tex->isArray) {
                srvViewType = tex->isCubemap ? SRVViewType::TextureCubeArray : SRVViewType::Texture2DArray;
            }
            else if (tex->isCubemap) {
                srvViewType = SRVViewType::TextureCube;
            }
            else {
                srvViewType = SRVViewType::Texture2D;
            }

            const rhi::Format srvFormat = tex->srvFormat == rhi::Format::Unknown ? tex->baseFormat : tex->srvFormat;
            const uint32_t srvSlices = (tex->isArray || tex->isCubemap) ? tex->arraySize : 1u;
            for (uint32_t slice = 0; slice < srvSlices; ++slice) {
                for (uint32_t mip = 0; mip < tex->mipLevels; ++mip) {
                    rhi::SrvDesc srvDesc{};
                    srvDesc.formatOverride = srvFormat;

                    if (tex->isCubemap) {
                        if (tex->isArray) {
                            srvDesc.dimension = rhi::SrvDim::TextureCubeArray;
                            srvDesc.cubeArray.mostDetailedMip = mip;
                            srvDesc.cubeArray.mipLevels = 1;
                            srvDesc.cubeArray.first2DArrayFace = slice * 6u;
                            srvDesc.cubeArray.numCubes = 1;
                        }
                        else {
                            srvDesc.dimension = rhi::SrvDim::TextureCube;
                            srvDesc.cube.mostDetailedMip = mip;
                            srvDesc.cube.mipLevels = 1;
                        }
                    }
                    else if (tex->isArray) {
                        srvDesc.dimension = rhi::SrvDim::Texture2DArray;
                        srvDesc.tex2DArray.mostDetailedMip = mip;
                        srvDesc.tex2DArray.mipLevels = 1;
                        srvDesc.tex2DArray.firstArraySlice = slice;
                        srvDesc.tex2DArray.arraySize = 1;
                        srvDesc.tex2DArray.planeSlice = 0;
                    }
                    else {
                        srvDesc.dimension = rhi::SrvDim::Texture2D;
                        srvDesc.tex2D.mostDetailedMip = mip;
                        srvDesc.tex2D.mipLevels = 1;
                        srvDesc.tex2D.planeSlice = 0;
                    }

                    const auto& slot = target.GetSRVInfo(srvViewType, mip, slice).slot;
                    device.CreateShaderResourceView({ slot.heap, slot.index }, apiResource.GetHandle(), srvDesc);
                }
            }

            if (tex->createCubemapAsArraySRV && tex->isCubemap) {
                for (uint32_t slice = 0; slice < 6u; ++slice) {
                    for (uint32_t mip = 0; mip < tex->mipLevels; ++mip) {
                        rhi::SrvDesc srvDesc{};
                        srvDesc.formatOverride = srvFormat;
                        srvDesc.dimension = rhi::SrvDim::Texture2DArray;
                        srvDesc.tex2DArray.mostDetailedMip = mip;
                        srvDesc.tex2DArray.mipLevels = 1;
                        srvDesc.tex2DArray.firstArraySlice = slice;
                        srvDesc.tex2DArray.arraySize = 1;
                        srvDesc.tex2DArray.planeSlice = 0;

                        const auto& slot = target.GetSRVInfo(SRVViewType::Texture2DArray, mip, slice).slot;
                        device.CreateShaderResourceView({ slot.heap, slot.index }, apiResource.GetHandle(), srvDesc);
                    }
                }
            }
        }

        if (tex->createUAV) {
            const rhi::Format uavFormat = tex->uavFormat == rhi::Format::Unknown && !rhi::helpers::IsSRGB(tex->baseFormat)
                ? tex->baseFormat
                : tex->uavFormat;
            const uint32_t uavSlices = (tex->isArray || tex->isCubemap) ? tex->totalArraySlices : 1u;
            for (uint32_t slice = 0; slice < uavSlices; ++slice) {
                for (uint32_t mip = 0; mip < tex->mipLevels; ++mip) {
                    rhi::UavDesc uavDesc{};
                    uavDesc.formatOverride = uavFormat;
                    if (tex->isArray || tex->isCubemap) {
                        uavDesc.dimension = rhi::UavDim::Texture2DArray;
                        uavDesc.texture2DArray.mipSlice = mip + tex->uavFirstMip;
                        uavDesc.texture2DArray.firstArraySlice = slice;
                        uavDesc.texture2DArray.arraySize = 1;
                        uavDesc.texture2DArray.planeSlice = 0;
                    }
                    else {
                        uavDesc.dimension = rhi::UavDim::Texture2D;
                        uavDesc.texture2D.mipSlice = mip + tex->uavFirstMip;
                        uavDesc.texture2D.planeSlice = 0;
                    }

                    const auto& slot = target.GetUAVShaderVisibleInfo(mip, slice).slot;
                    device.CreateUnorderedAccessView({ slot.heap, slot.index }, apiResource.GetHandle(), uavDesc);
                }
            }
        }

        if (tex->createNonShaderVisibleUAV) {
            const rhi::Format uavFormat = tex->uavFormat == rhi::Format::Unknown ? tex->baseFormat : tex->uavFormat;
            const uint32_t uavSlices = (tex->isArray || tex->isCubemap) ? tex->totalArraySlices : 1u;
            for (uint32_t slice = 0; slice < uavSlices; ++slice) {
                for (uint32_t mip = 0; mip < tex->mipLevels; ++mip) {
                    rhi::UavDesc uavDesc{};
                    uavDesc.formatOverride = uavFormat;
                    if (tex->isArray || tex->isCubemap) {
                        uavDesc.dimension = rhi::UavDim::Texture2DArray;
                        uavDesc.texture2DArray.mipSlice = mip + tex->uavFirstMip;
                        uavDesc.texture2DArray.firstArraySlice = slice;
                        uavDesc.texture2DArray.arraySize = 1;
                        uavDesc.texture2DArray.planeSlice = 0;
                    }
                    else {
                        uavDesc.dimension = rhi::UavDim::Texture2D;
                        uavDesc.texture2D.mipSlice = mip + tex->uavFirstMip;
                        uavDesc.texture2D.planeSlice = 0;
                    }

                    const auto& slot = target.GetUAVNonShaderVisibleInfo(mip, slice).slot;
                    device.CreateUnorderedAccessView({ slot.heap, slot.index }, apiResource.GetHandle(), uavDesc);
                }
            }
        }

        if (tex->createRTV) {
            const rhi::Format rtvFormat = tex->rtvFormat == rhi::Format::Unknown ? tex->baseFormat : tex->rtvFormat;
            const uint32_t rtvSlices = (tex->isArray || tex->isCubemap) ? tex->totalArraySlices : 1u;
            for (uint32_t slice = 0; slice < rtvSlices; ++slice) {
                for (uint32_t mip = 0; mip < tex->mipLevels; ++mip) {
                    rhi::RtvDesc rtvDesc{};
                    rtvDesc.formatOverride = rtvFormat;
                    rtvDesc.dimension = (tex->isArray || tex->isCubemap) ? rhi::RtvDim::Texture2DArray : rhi::RtvDim::Texture2D;
                    rtvDesc.range = {
                        static_cast<uint32_t>(mip),
                        1u,
                        (tex->isArray || tex->isCubemap) ? slice : 0u,
                        1u
                    };

                    const auto& slot = target.GetRTVInfo(mip, slice).slot;
                    device.CreateRenderTargetView({ slot.heap, slot.index }, apiResource.GetHandle(), rtvDesc);
                }
            }
        }

        if (tex->createDSV) {
            const rhi::Format dsvFormat = tex->dsvFormat == rhi::Format::Unknown ? tex->baseFormat : tex->dsvFormat;
            const uint32_t dsvSlices = (tex->isArray || tex->isCubemap) ? tex->totalArraySlices : 1u;
            for (uint32_t slice = 0; slice < dsvSlices; ++slice) {
                for (uint32_t mip = 0; mip < tex->mipLevels; ++mip) {
                    rhi::DsvDesc dsvDesc{};
                    dsvDesc.formatOverride = dsvFormat;
                    dsvDesc.dimension = (tex->isArray || tex->isCubemap) ? rhi::DsvDim::Texture2DArray : rhi::DsvDim::Texture2D;
                    dsvDesc.range = {
                        static_cast<uint32_t>(mip),
                        1u,
                        (tex->isArray || tex->isCubemap) ? slice : 0u,
                        1u
                    };

                    const auto& slot = target.GetDSVInfo(mip, slice).slot;
                    device.CreateDepthStencilView({ slot.heap, slot.index }, apiResource.GetHandle(), dsvDesc);
                }
            }
        }

        return;
    }

    if (const auto* buf = std::get_if<ViewRequirements::BufferViews>(&req.views)) {
        if (buf->createCBV) {
            const auto& slot = target.GetCBVInfo().slot;
            device.CreateConstantBufferView(
                { slot.heap, slot.index },
                apiResource.GetHandle(),
                buf->cbvDesc);
        }

        if (buf->createSRV) {
            const auto& slot = target.GetSRVInfo(SRVViewType::Buffer, 0, 0).slot;
            device.CreateShaderResourceView(
                { slot.heap, slot.index },
                apiResource.GetHandle(),
                buf->srvDesc);
        }

        if (buf->createUAV) {
            const auto& slot = target.GetUAVShaderVisibleInfo(0, 0).slot;
            device.CreateUnorderedAccessView(
                { slot.heap, slot.index },
                apiResource.GetHandle(),
                buf->uavDesc);
        }

        if (buf->createNonShaderVisibleUAV) {
            const auto& slot = target.GetUAVNonShaderVisibleInfo(0, 0).slot;
            device.CreateUnorderedAccessView(
                { slot.heap, slot.index },
                apiResource.GetHandle(),
                buf->uavDesc);
        }

        return;
    }

    spdlog::error("DescriptorHeapManager::UpdateDescriptorContents: invalid ViewRequirements variant");
    throw std::runtime_error("DescriptorHeapManager::UpdateDescriptorContents: invalid ViewRequirements");
}

rhi::DescriptorHeap DescriptorHeapManager::GetSRVDescriptorHeap() const {
    if (!m_cbvSrvUavHeap) {
        return {};
    }
    return m_cbvSrvUavHeap->GetHeap();
}

rhi::DescriptorHeap DescriptorHeapManager::GetSamplerDescriptorHeap() const {
    if (!m_samplerHeap) {
        return {};
    }
    return m_samplerHeap->GetHeap();
}

UINT DescriptorHeapManager::CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc) {
    if (!m_samplerHeap) {
        spdlog::error("DescriptorHeapManager::CreateIndexedSampler called before DescriptorHeapManager::Initialize");
        throw std::runtime_error("DescriptorHeapManager::CreateIndexedSampler called before DescriptorHeapManager::Initialize");
    }

    auto device = DeviceManager::GetInstance().GetDevice();
    UINT index = m_samplerHeap->AllocateDescriptor();
    device.CreateSampler({ m_samplerHeap->GetHeap().GetHandle(), index }, samplerDesc);
    return index;
}
