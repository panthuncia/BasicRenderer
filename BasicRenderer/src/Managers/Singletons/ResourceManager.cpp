#include "Managers/Singletons/ResourceManager.h"

#include <rhi_helpers.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"
void ResourceManager::Initialize() {

	auto device = DeviceManager::GetInstance().GetDevice();
	m_cbvSrvUavHeap = std::make_shared<DescriptorHeap>(
        device, 
        rhi::DescriptorHeapType::CbvSrvUav, 
        1000000 /*D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1*/, 
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

	m_perFrameBuffer = CreateIndexedConstantBuffer(sizeof(PerFrameCB), "PerFrameCB");

	perFrameCBData.ambientLighting = DirectX::XMVectorSet(0.1f, 0.1f, 0.1f, 1.0f);
	perFrameCBData.numShadowCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades")();
	auto shadowCascadeSplits = SettingsManager::GetInstance().getSettingGetter<std::vector<float>>("directionalLightCascadeSplits")();
	switch (perFrameCBData.numShadowCascades) {
	case 1:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], 0, 0, 0);
		break;
	case 2:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], 0, 0);
		break;
	case 3:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], shadowCascadeSplits[2], 0);
		break;
	case 4:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], shadowCascadeSplits[2], shadowCascadeSplits[3]);
	}

	auto result = device.CreateCommittedResource(rhi::helpers::ResourceDesc::Buffer(sizeof(UINT), rhi::HeapType::Upload), m_uavCounterReset);

	void* pMappedCounterReset = nullptr;
	
    m_uavCounterReset->Map(&pMappedCounterReset, 0, 0);
	ZeroMemory(pMappedCounterReset, sizeof(UINT));
	m_uavCounterReset->Unmap(0, 0);
}

rhi::DescriptorHeap ResourceManager::GetSRVDescriptorHeap() {
	return m_cbvSrvUavHeap->GetHeap();
}

rhi::DescriptorHeap ResourceManager::GetSamplerDescriptorHeap() {
	return m_samplerHeap->GetHeap();
}


void ResourceManager::UpdatePerFrameBuffer(UINT cameraIndex, UINT numLights, DirectX::XMUINT2 screenRes, DirectX::XMUINT3 clusterSizes, unsigned int frameIndex) {
	perFrameCBData.mainCameraIndex = cameraIndex;
	perFrameCBData.numLights = numLights;
	perFrameCBData.screenResX = screenRes.x;
	perFrameCBData.screenResY = screenRes.y;
	perFrameCBData.lightClusterGridSizeX = clusterSizes.x;
	perFrameCBData.lightClusterGridSizeY = clusterSizes.y;
	perFrameCBData.lightClusterGridSizeZ = clusterSizes.z;
	perFrameCBData.nearClusterCount = 4;
	perFrameCBData.clusterZSplitDepth = 6.0f;
	perFrameCBData.frameIndex = frameIndex % 64; // Wrap around every 64 frames

	BUFFER_UPLOAD(&perFrameCBData, sizeof(PerFrameCB), UploadManager::UploadTarget::FromShared(m_perFrameBuffer), 0);
}

UINT ResourceManager::CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc) {
	auto device = DeviceManager::GetInstance().GetDevice();

	UINT index = m_samplerHeap->AllocateDescriptor();

	device.CreateSampler({ m_samplerHeap->GetHeap().GetHandle(), index}, samplerDesc);
	return index;
}

void ResourceManager::AssignDescriptorSlots(
    GloballyIndexedResource& target,
    rhi::Resource& apiResource,
    const ViewRequirements& req)
{
    ReserveDescriptorSlots(target, req);
    UpdateDescriptorContents(target, apiResource, req);
}

void ResourceManager::ReserveDescriptorSlots(
    GloballyIndexedResource& target,
    const ViewRequirements& req)
{
    if (target.HasAnyDescriptorSlots()) {
        return;
    }

    if (!m_cbvSrvUavHeap || !m_samplerHeap || !m_rtvHeap || !m_dsvHeap || !m_nonShaderVisibleHeap) {
        spdlog::error("ResourceManager::ReserveDescriptorSlots called before ResourceManager::Initialize");
        throw std::runtime_error("ResourceManager::ReserveDescriptorSlots called before ResourceManager::Initialize");
    }

    if (const auto* tex = std::get_if<ViewRequirements::TextureViews>(&req.views))
    {
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

    if (const auto* buf = std::get_if<ViewRequirements::BufferViews>(&req.views))
    {
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

    spdlog::error("ResourceManager::ReserveDescriptorSlots: invalid ViewRequirements variant");
    throw std::runtime_error("ResourceManager::ReserveDescriptorSlots: invalid ViewRequirements");
}

void ResourceManager::UpdateDescriptorContents(
    GloballyIndexedResource& target,
    rhi::Resource& apiResource,
    const ViewRequirements& req)
{
    auto device = DeviceManager::GetInstance().GetDevice();

    if (!m_cbvSrvUavHeap || !m_samplerHeap || !m_rtvHeap || !m_dsvHeap || !m_nonShaderVisibleHeap) {
        spdlog::error("ResourceManager::UpdateDescriptorContents called before ResourceManager::Initialize");
        throw std::runtime_error("ResourceManager::UpdateDescriptorContents called before ResourceManager::Initialize");
    }

    // Texture path
    if (const auto* tex = std::get_if<ViewRequirements::TextureViews>(&req.views))
    {
        // SRV
        if (tex->createSRV)
        {
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

            // View cubemap as Texture2DArray
            if (tex->createCubemapAsArraySRV && tex->isCubemap)
            {
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

        // UAV (shader visible)
        if (tex->createUAV)
        {
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

        // UAV (non-shader visible)
        if (tex->createNonShaderVisibleUAV)
        {
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

        // RTV
        if (tex->createRTV)
        {
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

        // DSV
        if (tex->createDSV)
        {
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

    // Buffer path
    if (const auto* buf = std::get_if<ViewRequirements::BufferViews>(&req.views))
    {
        // CBV
        if (buf->createCBV)
        {
            const auto& slot = target.GetCBVInfo().slot;
            device.CreateConstantBufferView(
                { slot.heap, slot.index },
                apiResource.GetHandle(),
                buf->cbvDesc);
        }

        // SRV
        if (buf->createSRV)
        {
            const auto& slot = target.GetSRVInfo(SRVViewType::Buffer, 0, 0).slot;
            device.CreateShaderResourceView(
                { slot.heap, slot.index },
                apiResource.GetHandle(),
                buf->srvDesc);
        }

        // UAV (shader visible)
        if (buf->createUAV)
        {
            const auto& slot = target.GetUAVShaderVisibleInfo(0, 0).slot;
            device.CreateUnorderedAccessView(
                { slot.heap, slot.index },
                apiResource.GetHandle(),
                buf->uavDesc);
        }

        // UAV (non-shader visible)
        if (buf->createNonShaderVisibleUAV)
        {
            const auto& slot = target.GetUAVNonShaderVisibleInfo(0, 0).slot;
            device.CreateUnorderedAccessView(
                { slot.heap, slot.index },
                apiResource.GetHandle(),
                buf->uavDesc);
        }

        return;
    }

    spdlog::error("ResourceManager::UpdateDescriptorContents: invalid ViewRequirements variant");
    throw std::runtime_error("ResourceManager::UpdateDescriptorContents: invalid ViewRequirements");
}

void ResourceManager::Cleanup()
{
	m_perFrameBuffer.reset();
	m_cbvSrvUavHeap.reset();
	m_samplerHeap.reset();
	m_rtvHeap.reset();
	m_dsvHeap.reset();
	m_nonShaderVisibleHeap.reset();
	m_uavCounterReset.Reset();
}