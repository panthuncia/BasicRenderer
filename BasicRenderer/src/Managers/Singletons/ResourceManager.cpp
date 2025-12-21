#include "Managers/Singletons/ResourceManager.h"

#include <rhi_helpers.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Resources/Buffers/DynamicBuffer.h"
#include "Resources/Buffers/SortedUnsignedIntBuffer.h"
#include "Managers/Singletons/UploadManager.h"
void ResourceManager::Initialize() {
	//for (int i = 0; i < 3; i++) {
	//    frameResourceCopies[i] = std::make_unique<FrameResource>();
	//    frameResourceCopies[i]->Initialize();
	//}

	auto device = DeviceManager::GetInstance().GetDevice();
	m_cbvSrvUavHeap = std::make_shared<DescriptorHeap>(device, rhi::DescriptorHeapType::CbvSrvUav, 1000000 /*D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1*/, true);
	m_samplerHeap = std::make_shared<DescriptorHeap>(device, rhi::DescriptorHeapType::Sampler, 2048, true);
	m_rtvHeap = std::make_shared<DescriptorHeap>(device, rhi::DescriptorHeapType::RTV, 10000, false);
	m_dsvHeap = std::make_shared<DescriptorHeap>(device, rhi::DescriptorHeapType::DSV, 10000, false);
	m_nonShaderVisibleHeap = std::make_shared<DescriptorHeap>(device, rhi::DescriptorHeapType::CbvSrvUav, 100000, false);

	m_perFrameBuffer = CreateIndexedConstantBuffer<PerFrameCB>(L"PerFrameCB");

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

	BUFFER_UPLOAD(&perFrameCBData, sizeof(PerFrameCB), m_perFrameBuffer, 0);
}

UINT ResourceManager::CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc) {
	auto device = DeviceManager::GetInstance().GetDevice();

	UINT index = m_samplerHeap->AllocateDescriptor();
	//D3D12_CPU_DESCRIPTOR_HANDLE handle = m_samplerHeap->GetCPUHandle(index);

	//device->CreateSampler(&samplerDesc, handle);
	device.CreateSampler({ m_samplerHeap->GetHeap().GetHandle(), index}, samplerDesc);
	return index;
}

//std::shared_ptr<DynamicBuffer> ResourceManager::CreateIndexedDynamicBuffer(size_t elementSize, size_t numElements, std::wstring name, bool byteAddress, bool UAV) {
//#if defined(_DEBUG)
//	assert(numElements > 0 && byteAddress ? elementSize == 1 : (elementSize > 0 && elementSize % 4 == 0));
//	assert(byteAddress ? numElements % 4 == 0 : true);
//#endif
//	auto device = DeviceManager::GetInstance().GetDevice();
//
//	size_t bufferSize = elementSize * numElements;
//	{
//		const size_t align = 4;
//		const size_t rem = bufferSize % align;
//		if (rem) bufferSize += (align - rem); // Align up to 4 bytes
//	}
//	// Create the dynamic structured buffer instance
//	std::shared_ptr<DynamicBuffer> pDynamicBuffer = DynamicBuffer::CreateShared(byteAddress, elementSize, bufferSize, name, UAV);
//
//	pDynamicBuffer->SetOnResized([this](size_t typeSize, size_t capacity, bool byteAddress, DynamicBufferBase* buffer, bool UAV) {
//		this->onDynamicBufferResized(typeSize, capacity, byteAddress, buffer, UAV);
//		});
//
//	// Create an SRV for the buffer
//
//	UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
//
//	device.CreateShaderResourceView(
//		{ m_cbvSrvUavHeap->GetHeap().GetHandle(), index},
//		pDynamicBuffer->GetBuffer()->GetAPIResource().GetHandle(),
//		{
//			.dimension = rhi::SrvDim::Buffer,
//			.formatOverride = byteAddress ? rhi::Format::R32_Typeless : rhi::Format::Unknown,
//			.buffer = {
//				.kind = byteAddress ? rhi::BufferViewKind::Raw : rhi::BufferViewKind::Structured,
//				.firstElement = 0,
//				.numElements = static_cast<uint32_t>(byteAddress ? numElements / 4 : numElements),
//				.structureByteStride = static_cast<uint32_t>(byteAddress ? 0 : elementSize)
//			}
//		});
//
//	ShaderVisibleIndexInfo srvInfo;
//	srvInfo.slot.index = index;
//	srvInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();
//
//	pDynamicBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});
//
//	if (UAV) {
//		// Shader visible UAV
//		unsigned int uavShaderVisibleIndex = m_cbvSrvUavHeap->AllocateDescriptor();
//		device.CreateUnorderedAccessView(
//			{ m_cbvSrvUavHeap->GetHeap().GetHandle(), uavShaderVisibleIndex},
//			pDynamicBuffer->GetBuffer()->GetAPIResource().GetHandle(),
//			{
//				.dimension = rhi::UavDim::Buffer,
//				.buffer = {
//					.kind = byteAddress ? rhi::BufferViewKind::Raw : rhi::BufferViewKind::Structured,
//					.firstElement = 0,
//					.numElements = static_cast<uint32_t>(byteAddress ? numElements / 4 : numElements),
//					.structureByteStride = static_cast<uint32_t>(byteAddress ? 0 : elementSize)
//				}
//			});
//
//		ShaderVisibleIndexInfo uavInfo;
//		uavInfo.slot.index = uavShaderVisibleIndex;
//		uavInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();
//		pDynamicBuffer->SetUAVGPUDescriptors(m_cbvSrvUavHeap, {{uavInfo}}, 0);
//	}
//
//	return pDynamicBuffer;
//}

std::shared_ptr<SortedUnsignedIntBuffer> ResourceManager::CreateIndexedSortedUnsignedIntBuffer(uint64_t capacity, std::wstring name) {
	auto device = DeviceManager::GetInstance().GetDevice();

	std::shared_ptr<SortedUnsignedIntBuffer> pBuffer = SortedUnsignedIntBuffer::CreateShared(capacity, name);

	pBuffer->SetOnResized([this](UINT capacity, UINT numElements, DynamicBufferBase* buffer) {
		this->onDynamicStructuredBufferResized(capacity, numElements, buffer, false);
		});

	// Create an SRV for the buffer
	UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
	device.CreateShaderResourceView(
		{ m_cbvSrvUavHeap->GetHeap().GetHandle(), index},
		pBuffer->GetBuffer()->GetAPIResource().GetHandle(),
		{
			.dimension = rhi::SrvDim::Buffer,
			.formatOverride = rhi::Format::Unknown,
			.buffer = {
				.kind = rhi::BufferViewKind::Structured,
				.firstElement = 0,
				.numElements = static_cast<uint32_t>(capacity),
				.structureByteStride = 4
			}
		});

	ShaderVisibleIndexInfo srvInfo;
	srvInfo.slot.index = index;
	srvInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();

	pBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

	return pBuffer;
}

void ResourceManager::AssignDescriptorSlots(
    GloballyIndexedResource& target,
    rhi::Resource& apiResource,
    const ViewRequirements& req)
{
    auto device = DeviceManager::GetInstance().GetDevice();

    if (!m_cbvSrvUavHeap || !m_samplerHeap || !m_rtvHeap || !m_dsvHeap || !m_nonShaderVisibleHeap) {
        spdlog::error("ResourceManager::AssignDescriptorSlots called before ResourceManager::Initialize");
        throw std::runtime_error("ResourceManager::AssignDescriptorSlots called before ResourceManager::Initialize");
    }

    // Texture path
    if (const auto* tex = std::get_if<ViewRequirements::TextureViews>(&req.views))
    {
        const auto& cbvSrvUavHeap = m_cbvSrvUavHeap;
        const auto& nonShaderVisibleHeap = m_nonShaderVisibleHeap;
        const auto& rtvHeap = m_rtvHeap;
        const auto& dsvHeap = m_dsvHeap;

        // SRV
        if (tex->createSRV)
        {
            auto srvInfos = CreateShaderResourceViewsPerMip(
                device,
                apiResource,
                tex->srvFormat == rhi::Format::Unknown ? tex->baseFormat : tex->srvFormat,
                cbvSrvUavHeap.get(),
                tex->mipLevels,
                tex->isCubemap,
                tex->isArray,
                tex->arraySize);

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

            target.SetDefaultSRVViewType(srvViewType);
            target.SetSRVView(srvViewType, cbvSrvUavHeap, srvInfos);

            // Optional: view cubemap as Texture2DArray
            if (tex->createCubemapAsArraySRV && tex->isCubemap)
            {
                auto secondarySrvInfos = CreateShaderResourceViewsPerMip(
                    device,
                    apiResource,
                    tex->srvFormat == rhi::Format::Unknown ? tex->baseFormat : tex->srvFormat,
                    cbvSrvUavHeap.get(),
                    tex->mipLevels,
                    /*isCubemap*/ false,
                    /*isArray*/   tex->isArray,
                    /*arraySize*/ 6u);

                target.SetSRVView(SRVViewType::Texture2DArray, cbvSrvUavHeap, secondarySrvInfos);
            }
        }

        // UAV (shader visible)
        if (tex->createUAV)
        {
            auto uavInfos = CreateUnorderedAccessViewsPerMip(
                device,
                apiResource,
                tex->uavFormat == rhi::Format::Unknown ? tex->baseFormat : tex->uavFormat,
                cbvSrvUavHeap.get(),
                tex->mipLevels,
                tex->isArray,
                tex->totalArraySlices,
                tex->uavFirstMip,
                tex->isCubemap);

            target.SetUAVGPUDescriptors(cbvSrvUavHeap, uavInfos);
        }

        // UAV (non-shader visible)
        if (tex->createNonShaderVisibleUAV)
        {
            auto nonShaderUavInfos = CreateNonShaderVisibleUnorderedAccessViewsPerMip(
                device,
                apiResource,
                tex->uavFormat == rhi::Format::Unknown ? tex->baseFormat : tex->uavFormat,
                nonShaderVisibleHeap.get(),
                tex->mipLevels,
                tex->isArray,
                tex->arraySize,
                tex->uavFirstMip);

            target.SetUAVCPUDescriptors(nonShaderVisibleHeap, nonShaderUavInfos);
        }

        // RTV
        if (tex->createRTV)
        {
            auto rtvInfos = CreateRenderTargetViews(
                device,
                apiResource,
                tex->rtvFormat == rhi::Format::Unknown ? tex->baseFormat : tex->rtvFormat,
                rtvHeap.get(),
                tex->isCubemap,
                tex->isArray,
                tex->arraySize,
                tex->mipLevels);

            target.SetRTVDescriptors(rtvHeap, rtvInfos);
        }

        // DSV
        if (tex->createDSV)
        {
            auto dsvInfos = CreateDepthStencilViews(
                device,
                apiResource,
                dsvHeap.get(),
                tex->dsvFormat == rhi::Format::Unknown ? tex->baseFormat : tex->dsvFormat,
                tex->isCubemap,
                tex->isArray,
                tex->arraySize,
                tex->mipLevels);

            target.SetDSVDescriptors(dsvHeap, dsvInfos);
        }

        return;
    }

    // Buffer path
    if (const auto* buf = std::get_if<ViewRequirements::BufferViews>(&req.views))
    {
        // CBV
        if (buf->createCBV)
        {
            const uint32_t index = m_cbvSrvUavHeap->AllocateDescriptor();
            ShaderVisibleIndexInfo cbvInfo{};
            cbvInfo.slot.index = index;
            cbvInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();

            target.SetCBVDescriptor(m_cbvSrvUavHeap, cbvInfo);
            device.CreateConstantBufferView(
                { m_cbvSrvUavHeap->GetHeap().GetHandle(), index },
                apiResource.GetHandle(),
                buf->cbvDesc);
        }

        // SRV
        if (buf->createSRV)
        {
            const uint32_t index = m_cbvSrvUavHeap->AllocateDescriptor();
            ShaderVisibleIndexInfo srvInfo{};
            srvInfo.slot.index = index;
            srvInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();

            device.CreateShaderResourceView(
                { m_cbvSrvUavHeap->GetHeap().GetHandle(), index },
                apiResource.GetHandle(),
                buf->srvDesc);

            target.SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, { { srvInfo } });
        }

        // UAV (shader visible)
        if (buf->createUAV)
        {
            const uint32_t index = m_cbvSrvUavHeap->AllocateDescriptor();
            ShaderVisibleIndexInfo uavInfo{};
            uavInfo.slot.index = index;
            uavInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();

            device.CreateUnorderedAccessView(
                { m_cbvSrvUavHeap->GetHeap().GetHandle(), index },
                apiResource.GetHandle(),
                buf->uavDesc);

            target.SetUAVGPUDescriptors(m_cbvSrvUavHeap, { { uavInfo } }, buf->uavCounterOffset);
        }

        // UAV (non-shader visible)
        if (buf->createNonShaderVisibleUAV)
        {
            const uint32_t index = m_nonShaderVisibleHeap->AllocateDescriptor();
            NonShaderVisibleIndexInfo uavInfo{};
            uavInfo.slot.index = index;
            uavInfo.slot.heap = m_nonShaderVisibleHeap->GetHeap().GetHandle();

            device.CreateUnorderedAccessView(
                { m_nonShaderVisibleHeap->GetHeap().GetHandle(), index },
                apiResource.GetHandle(),
                buf->uavDesc);

            target.SetUAVCPUDescriptors(m_nonShaderVisibleHeap, { { uavInfo } });
        }

        return;
    }

    spdlog::error("ResourceManager::AssignDescriptorSlots: invalid ViewRequirements variant");
    throw std::runtime_error("ResourceManager::AssignDescriptorSlots: invalid ViewRequirements");
}

void ResourceManager::Cleanup()
{
	m_perFrameBuffer.reset();
}