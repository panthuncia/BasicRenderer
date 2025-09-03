#pragma once

#include <wrl.h>
#include <vector>
#include <stdexcept>
#include <queue>

#include <rhi.h>

#include "ShaderBuffers.h"
#include "spdlog/spdlog.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Buffers/Buffer.h"
#include "Render/DescriptorHeap.h"
#include "Resources/ResourceStates.h"
#include "Render/RenderContext.h"
#include "Utilities/Utilities.h"
#include "Resources/TextureDescription.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"

using namespace Microsoft::WRL;

class BufferView;
class SortedUnsignedIntBuffer;

class ResourceManager {
public:
    static ResourceManager& GetInstance() {
        static ResourceManager instance;
        return instance;
    }

    void Initialize(rhi::Queue commandQueue);

    rhi::DescriptorHeapHandle GetSRVDescriptorHeap();
    rhi::DescriptorHeapHandle GetSamplerDescriptorHeap();
    void UpdatePerFrameBuffer(UINT cameraIndex, UINT numLights, DirectX::XMUINT2 screenRes, DirectX::XMUINT3 clusterSizes, unsigned int frameIndex);
    
    std::shared_ptr<Buffer>& GetPerFrameBuffer() {
		return perFrameBufferHandle;
    }

	void SetDirectionalCascadeSplits(const std::vector<float>& splits) {
        switch (perFrameCBData.numShadowCascades) {
        case 1:
            perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(splits[0], 0, 0, 0);
            break;
        case 2:
            perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(splits[0], splits[1], 0, 0);
            break;
        case 3:
            perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(splits[0], splits[1], splits[2], 0);
            break;
        case 4:
            perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(splits[0], splits[1], splits[2], splits[3]);
        }
	}

    template<typename T>
    std::shared_ptr<Buffer> CreateIndexedConstantBuffer(std::wstring name = L"") {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for constant buffers.");

        auto& device = DeviceManager::GetInstance().GetDevice();

        // Calculate the size of the buffer to be 256-byte aligned
        UINT bufferSize = (sizeof(T) + 255) & ~255;

        // Create the buffer
        //bufferHandle.uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, bufferSize, true, false);
        auto dataBuffer = Buffer::CreateShared(device, ResourceCPUAccessType::NONE, bufferSize, false, false);
		dataBuffer->SetName(name);
//        ResourceTransition transition;
//		transition.resource = dataBuffer.get();
//		transition.beforeState = ResourceState::UNKNOWN;
//		transition.afterState = ResourceState::CONSTANT;
//#if defined(_DEBUG)
//		transition.name = name;
//#endif
//		QueueResourceTransition(transition);
        // Create a descriptor for the buffer
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = dataBuffer->m_buffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = bufferSize;

        UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cbvSrvUavHeap->GetCPUHandle(index);

        ShaderVisibleIndexInfo cbvInfo;
        cbvInfo.index = index;
        cbvInfo.gpuHandle = m_cbvSrvUavHeap->GetGPUHandle(index);
        dataBuffer->SetCBVDescriptor(m_cbvSrvUavHeap, cbvInfo);

        device.CreateConstantBufferView()

        return dataBuffer;
    }

    std::shared_ptr<Buffer> CreateIndexedStructuredBuffer(size_t numElements, unsigned int elementSize, bool hasUploadBuffer = true, bool UAV = false, bool UAVCounter = false) {
        auto device = DeviceManager::GetInstance().GetDevice();
        size_t bufferSize = numElements * elementSize;
        size_t counterOffset = 0;
		if (UAVCounter) {
            size_t requiredSize = (numElements * elementSize) + sizeof(UINT); // Add space for the counter
            unsigned int alignment = elementSize; // Buffer should be a multiple of sizeof(T)

            // Ensure bufferSize is a multiple of typeSize and meets requiredSize
            bufferSize = ((requiredSize + alignment - 1) / alignment) * alignment;

            // Find the next 4096-aligned address after requiredSize
            size_t potentialCounterOffset = (requiredSize + 4095) & ~4095;

            // If the 4096-aligned address is within the buffer, we can use it
            if (potentialCounterOffset + sizeof(unsigned int) <= bufferSize) {
                counterOffset = potentialCounterOffset;
            }
            else {
                // Otherwise, expand the buffer to fit the 4096-aligned counter offset
                bufferSize = ((potentialCounterOffset + sizeof(unsigned int) + alignment - 1) / alignment) * alignment;
                counterOffset = potentialCounterOffset;
            }

            assert(counterOffset % 4096 == 0);
		}

        auto dataBuffer = Buffer::CreateShared(device, ResourceCPUAccessType::NONE, bufferSize, false, UAV);
        
        //ResourceTransition transition = { dataBuffer.get(), ResourceState::UNKNOWN,  usageType };
//#if defined(_DEBUG)
//        transition.name = L"IndexedStructuredBuffer";
//#endif
//        QueueResourceTransition(transition);

        unsigned int index = m_cbvSrvUavHeap->AllocateDescriptor();

		rhi::SrvDesc srvDesc = {};
		srvDesc.formatOverride = rhi::Format::Unknown;
		srvDesc.dim = rhi::SrvDim::Buffer;
		srvDesc.buffer.numElements = static_cast<UINT>(numElements);
		srvDesc.buffer.structureByteStride = static_cast<UINT>(elementSize);
		srvDesc.buffer.kind = rhi::BufferViewKind::Structured;

        device.CreateShaderResourceView({ m_cbvSrvUavHeap->GetHeap(), index }, srvDesc);

        ShaderVisibleIndexInfo srvInfo;
        srvInfo.index = index;
        dataBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

        if (UAV) {
            rhi::UavDesc uavDesc = {};
			uavDesc.type = rhi::UAVType::Buffer;
			uavDesc.resource = dataBuffer->m_buffer->GetHandle();
			uavDesc.bufKind = rhi::BufferViewKind::Structured;
			uavDesc.bufFormat = rhi::Format::Unknown;
			uavDesc.numElements = static_cast<UINT>(numElements);
			uavDesc.structureByteStride = static_cast<UINT>(elementSize);
            if (UAVCounter) {
                uavDesc.counterOffsetBytes = counterOffset;
            }
            else {
                uavDesc.counterOffsetBytes = 0;
			}

			// Shader visible UAV
			unsigned int uavShaderVisibleIndex = m_cbvSrvUavHeap->AllocateDescriptor();
			device.CreateUnorderedAccessView({ m_cbvSrvUavHeap->GetHeap(), uavShaderVisibleIndex }, uavDesc);

			ShaderVisibleIndexInfo uavInfo;
			uavInfo.index = uavShaderVisibleIndex;
            dataBuffer->SetUAVGPUDescriptors(m_cbvSrvUavHeap, {{uavInfo}}, counterOffset);

        }

        return dataBuffer;
    }

    template<typename T>
    std::shared_ptr<DynamicStructuredBuffer<T>> CreateIndexedDynamicStructuredBuffer(UINT capacity = 64, std::wstring name = "", bool UAV = false) {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for structured buffers.");

        auto device = DeviceManager::GetInstance().GetDevice();

        // Create the dynamic structured buffer instance
        UINT bufferID = GetNextResizableBufferID();
        std::shared_ptr<DynamicStructuredBuffer<T>> pDynamicBuffer = DynamicStructuredBuffer<T>::CreateShared(bufferID, capacity, name, UAV);

        pDynamicBuffer->SetOnResized([this](UINT bufferID, UINT typeSize, UINT capacity, DynamicBufferBase* buffer) {
            this->onDynamicStructuredBufferResized(bufferID, typeSize, capacity, buffer, false);
            });

        // Create an SRV for the buffer
        //D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        //srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        //srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        //srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        //srvDesc.Buffer.NumElements = capacity;
        //srvDesc.Buffer.StructureByteStride = sizeof(T);
        //srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

		rhi::SrvDesc srvDesc = {};
		srvDesc.formatOverride = rhi::Format::Unknown;
		srvDesc.dim = rhi::SrvDim::Buffer;
		srvDesc.buffer.numElements = capacity;
		srvDesc.buffer.structureByteStride = sizeof(T);
		srvDesc.buffer.kind = rhi::BufferViewKind::Structured;



        UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
        bufferIDDescriptorIndexMap[bufferID] = index;
		device->CreateShaderResourceView({ m_cbvSrvUavHeap->GetHeap(), index }, srvDesc);

        ShaderVisibleIndexInfo srvInfo;
        srvInfo.index = index;
        pDynamicBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

        return pDynamicBuffer;
    }

    template<typename T>
    std::shared_ptr<LazyDynamicStructuredBuffer<T>> CreateIndexedLazyDynamicStructuredBuffer(uint32_t capacity = 64, std::wstring name = "", uint64_t alignment = 1, bool UAV = false) {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for structured buffers.");

        auto& device = DeviceManager::GetInstance().GetDevice();

        // Create the dynamic structured buffer instance
        UINT bufferID = GetNextResizableBufferID();
        std::shared_ptr<LazyDynamicStructuredBuffer<T>> pDynamicBuffer = LazyDynamicStructuredBuffer<T>::CreateShared(bufferID, capacity, name, alignment, UAV);

        pDynamicBuffer->SetOnResized([this](UINT bufferID, uint32_t typeSize, uint32_t capacity, DynamicBufferBase* buffer, bool uav) {
            this->onDynamicStructuredBufferResized(bufferID, typeSize, capacity, buffer, uav);
            });

		rhi::SrvDesc srvDesc = {};
		srvDesc.formatOverride = rhi::Format::Unknown;
		srvDesc.dim = rhi::SrvDim::Buffer;
		srvDesc.buffer.numElements = capacity;
		srvDesc.buffer.structureByteStride = sizeof(T);
		srvDesc.buffer.kind = rhi::BufferViewKind::Structured;

        UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
        bufferIDDescriptorIndexMap[bufferID] = index;
		device.CreateShaderResourceView({ m_cbvSrvUavHeap->GetHeap(), index }, srvDesc);

		ShaderVisibleIndexInfo srvInfo;
		srvInfo.index = static_cast<int>(index);
        pDynamicBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

        if (UAV) {

			rhi::UavDesc uavDesc = {};
			uavDesc.type = rhi::UAVType::Buffer;
			uavDesc.resource = pDynamicBuffer->GetAPIResource().GetHandle();
			uavDesc.bufKind = rhi::BufferViewKind::Structured;
			uavDesc.bufFormat = rhi::Format::Unknown;
			uavDesc.numElements = capacity;
			uavDesc.structureByteStride = sizeof(T);
			uavDesc.counterOffsetBytes = 0;

            // Shader visible UAV
            unsigned int uavShaderVisibleIndex = m_cbvSrvUavHeap->AllocateDescriptor();
			device.CreateUnorderedAccessView({ m_cbvSrvUavHeap->GetHeap(), uavShaderVisibleIndex }, uavDesc);

            ShaderVisibleIndexInfo uavInfo;
            uavInfo.index = static_cast<int>(uavShaderVisibleIndex);
            uavInfo.gpuHandle = m_cbvSrvUavHeap->GetGPUHandle(uavShaderVisibleIndex);
            pDynamicBuffer->SetUAVGPUDescriptors(m_cbvSrvUavHeap, {{uavInfo}}, 0);
        }

        return pDynamicBuffer;
    }

    std::shared_ptr<DynamicBuffer> CreateIndexedDynamicBuffer(size_t elementSize, size_t numElements, std::wstring name, bool byteAddress = false, bool UAV = false);
	std::shared_ptr<SortedUnsignedIntBuffer> CreateIndexedSortedUnsignedIntBuffer(uint64_t capacity, std::wstring name = L"");

    UINT GetNextResizableBufferID() {
        UINT val = numResizableBuffers;
        numResizableBuffers++;
        return val;
    }

    void onDynamicStructuredBufferResized(UINT bufferID, uint32_t typeSize, uint32_t capacity, DynamicBufferBase* buffer, bool UAV) {
        UINT descriptorIndex = bufferIDDescriptorIndexMap[bufferID];
        auto device = DeviceManager::GetInstance().GetDevice();

        // Create a Shader Resource View for the buffer
		rhi::SrvDesc srvDesc = {};
		srvDesc.formatOverride = rhi::Format::Unknown;
		srvDesc.dim = rhi::SrvDim::Buffer;
		srvDesc.buffer.numElements = capacity;
		srvDesc.buffer.structureByteStride = typeSize;
		srvDesc.buffer.kind = rhi::BufferViewKind::Structured;
		srvDesc.buffer.firstElement = 0;
		srvDesc.buffer.numElements = capacity;
		srvDesc.buffer.structureByteStride = typeSize;

		device.CreateShaderResourceView({ m_cbvSrvUavHeap->GetHeap(), descriptorIndex }, srvDesc);

        if (UAV){

			rhi::UavDesc uavDesc = {};
			uavDesc.type = rhi::UAVType::Buffer;
            uavDesc.resource = buffer->GetAPIResource();
			uavDesc.bufKind = rhi::BufferViewKind::Structured;
			uavDesc.bufFormat = rhi::Format::Unknown;
			uavDesc.numElements = capacity;
			uavDesc.structureByteStride = typeSize;
			uavDesc.counterOffsetBytes = 0;

            // Shader visible UAV
            auto uavIndex = buffer->GetUAVShaderVisibleInfo(0).index;

			device.CreateUnorderedAccessView({ m_cbvSrvUavHeap->GetHeap(), static_cast<uint32_t>(uavIndex) }, uavDesc);
        }

        //		auto bufferState = buffer->GetState();
//		// After resize, internal buffer state will not match the wrapper state
//		if (bufferState != ResourceState::UNKNOWN) {
//			ResourceTransition transition;
//			transition.resource = buffer->m_dataBuffer.get();
//			transition.beforeState = ResourceState::UNKNOWN;
//			transition.afterState = buffer->GetState();
//#if defined(_DEBUG)
//            transition.name = buffer->GetName()+L": Resize";
//#endif
//			QueueResourceTransition(transition);
//		}
    }

    void onDynamicBufferResized(UINT bufferID, size_t elementSize, size_t numElements, bool byteAddress, DynamicBufferBase* buffer, bool UAV) {

        // If debug mode, check buffer size
#if BUILD_TYPE == BUILD_TYPE_DEBUG
		if (numElements * elementSize > std::numeric_limits<uint32_t>::max()) {
			spdlog::error("Buffer size exceeds maximum limit for ID: {}", bufferID);
			throw std::runtime_error("Buffer size exceeds maximum limit");
		}
#endif
        UINT descriptorIndex = bufferIDDescriptorIndexMap[bufferID];
        auto device = DeviceManager::GetInstance().GetDevice();

        // Create a Shader Resource View for the buffer

		rhi::SrvDesc srvDesc = {};
		srvDesc.formatOverride = byteAddress ? rhi::Format::R32_Typeless : rhi::Format::Unknown;
		srvDesc.dim = rhi::SrvDim::Buffer;
		srvDesc.buffer.firstElement = 0;
		srvDesc.buffer.numElements = static_cast<UINT>(byteAddress ? numElements / 4 : numElements);
		srvDesc.buffer.structureByteStride = static_cast<UINT>(byteAddress ? 0 : elementSize);
        srvDesc.buffer.kind = byteAddress ? rhi::BufferViewKind::Raw : rhi::BufferViewKind::Structured;
		
		device.CreateShaderResourceView({ m_cbvSrvUavHeap->GetHeap(), descriptorIndex }, srvDesc);

        if (UAV) {

			rhi::UavDesc uavDesc = {};
			uavDesc.type = rhi::UAVType::Buffer;
			uavDesc.resource = buffer->GetAPIResource();
			uavDesc.bufKind = byteAddress ? rhi::BufferViewKind::Raw : rhi::BufferViewKind::Structured;
			uavDesc.bufFormat = byteAddress ? rhi::Format::R32_Typeless : rhi::Format::Unknown;
			uavDesc.numElements = static_cast<UINT>(byteAddress ? numElements / 4 : numElements);
			uavDesc.structureByteStride = static_cast<UINT>(byteAddress ? 0 : elementSize);
			uavDesc.counterOffsetBytes = 0;

            // Shader visible UAV
			auto uavIndex = buffer->GetUAVShaderVisibleInfo(0).index;

			device.CreateUnorderedAccessView({ m_cbvSrvUavHeap->GetHeap(), static_cast<uint32_t>(uavIndex) }, uavDesc);
        }

    }

    template<typename T>
    std::shared_ptr<Buffer> CreateConstantBuffer(std::wstring name = L"") {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for constant buffers.");

		auto& device = DeviceManager::GetInstance().GetDevice();

		// Calculate the size of the buffer to be 256-byte aligned
		UINT bufferSize = (sizeof(T) + 255) & ~255;

        auto dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, bufferSize, false, false);
		dataBuffer->SetName(name);


        return dataBuffer;
    }

    std::shared_ptr<Buffer> CreateBuffer(size_t size, void* pInitialData, bool UAV = false);

    std::pair<rhi::ResourcePtr, rhi::HeapHandle> CreateTextureResource(
        const TextureDescription& desc,
        rhi::HeapHandle placedResourceHeap = {});

    void UploadTextureData(rhi::Resource& pResource, const TextureDescription& desc, const std::vector<const stbi_uc*>& initialData, unsigned int arraySize, unsigned int mipLevels);

    UINT CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc);

	//void QueueResourceTransition(const ResourceTransition& transition);
    //void ExecuteResourceTransitions();

	void SetActiveEnvironmentIndex(unsigned int index) { perFrameCBData.activeEnvironmentIndex = index; }
	void SetOutputType(unsigned int type) { perFrameCBData.outputType = type; }

	rhi::Resource GetUAVCounterReset() { return m_uavCounterReset.Get(); }

    const std::shared_ptr<DescriptorHeap>& GetCBVSRVUAVHeap() const { return m_cbvSrvUavHeap; }
    const std::shared_ptr<DescriptorHeap>& GetSamplerHeap() const { return m_samplerHeap; }
    const std::shared_ptr<DescriptorHeap>& GetRTVHeap() const { return m_rtvHeap; }
    const std::shared_ptr<DescriptorHeap>& GetDSVHeap() const { return m_dsvHeap; }
    const std::shared_ptr<DescriptorHeap>& GetNonShaderVisibleHeap() const { return m_nonShaderVisibleHeap; }
    
private:
    ResourceManager(){};
    void WaitForCopyQueue();
    void WaitForTransitionQueue();
    void InitializeCopyCommandQueue();
    void InitializeTransitionCommandList();
	void SetTransitionCommandQueue(rhi::Queue commandQueue);
    void GetCopyCommandList(rhi::CommandListPtr& commandList, rhi::CommandAllocatorPtr& commandAllocator);
    void GetDirectCommandList(rhi::CommandListPtr& commandList, rhi::CommandAllocatorPtr& commandAllocator);
    void ExecuteAndWaitForCommandList(rhi::CommandListPtr& commandList, rhi::CommandAllocatorPtr& commandAllocator);

    std::shared_ptr<DescriptorHeap> m_cbvSrvUavHeap;
    std::shared_ptr<DescriptorHeap> m_samplerHeap;
    std::shared_ptr<DescriptorHeap> m_rtvHeap;
    std::shared_ptr<DescriptorHeap> m_dsvHeap;
    std::shared_ptr<DescriptorHeap> m_nonShaderVisibleHeap;
    UINT numResizableBuffers;
    std::unordered_map<UINT, UINT> bufferIDDescriptorIndexMap;

    rhi::Queue copyCommandQueue;
    rhi::CommandAllocatorPtr copyCommandAllocator;
    rhi::CommandListPtr copyCommandList;
    rhi::TimelinePtr copyFence;
    HANDLE copyFenceEvent;
    UINT64 copyFenceValue = 0;

    rhi::Queue transitionCommandQueue;
    rhi::CommandAllocatorPtr transitionCommandAllocator;
    rhi::CommandListPtr transitionCommandList;
    rhi::TimelinePtr transitionFence;
    HANDLE transitionFenceEvent;
    UINT64 transitionFenceValue = 0;

    std::shared_ptr<Buffer> perFrameBufferHandle;
    UINT8* pPerFrameConstantBuffer;
    PerFrameCB perFrameCBData;
    UINT currentFrameIndex;

    //std::shared_ptr<DynamicStructuredBuffer<LightInfo>> lightBufferPtr;

    rhi::CommandListPtr commandList;
    rhi::CommandAllocatorPtr commandAllocator;

    std::vector<std::shared_ptr<Buffer>> buffersToUpdate;
    std::vector<DynamicBufferBase*> dynamicBuffersToUpdate;
	std::vector<ViewedDynamicBufferBase*> dynamicBuffersToUpdateViews;

	//std::vector<ResourceTransition> queuedResourceTransitions;

    rhi::ResourcePtr m_uavCounterReset;

	int defaultShadowSamplerIndex = -1;

};