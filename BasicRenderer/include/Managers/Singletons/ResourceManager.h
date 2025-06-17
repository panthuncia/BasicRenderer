#pragma once

#include <directx/d3d12.h>
#include <wrl.h>
#include <vector>
#include <stdexcept>
#include <queue>
#include "DirectX/d3dx12.h"
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

    void Initialize(ID3D12CommandQueue* commandQueue);

    CD3DX12_CPU_DESCRIPTOR_HANDLE GetSRVCPUHandle(UINT index);
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetSRVGPUHandle(UINT index);
    ComPtr<ID3D12DescriptorHeap> GetSRVDescriptorHeap();
    ComPtr<ID3D12DescriptorHeap> GetSamplerDescriptorHeap();
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
        auto dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, bufferSize, false, false);
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

        device->CreateConstantBufferView(&cbvDesc, handle);

        return dataBuffer;
    }

    std::shared_ptr<Buffer> CreateIndexedStructuredBuffer(size_t numElements, size_t elementSize, bool hasUploadBuffer = true, bool UAV = false, bool UAVCounter = false) {
        auto& device = DeviceManager::GetInstance().GetDevice();
        UINT bufferSize = numElements * elementSize;
        unsigned int counterOffset = 0;
		if (UAVCounter) {
            UINT requiredSize = (numElements * elementSize) + sizeof(UINT); // Add space for the counter
            UINT alignment = elementSize; // Buffer should be a multiple of sizeof(T)

            // Ensure bufferSize is a multiple of typeSize and meets requiredSize
            bufferSize = ((requiredSize + alignment - 1) / alignment) * alignment;

            // Find the next 4096-aligned address after requiredSize
            UINT potentialCounterOffset = (requiredSize + 4095) & ~4095;

            // If the 4096-aligned address is within the buffer, we can use it
            if (potentialCounterOffset + sizeof(UINT) <= bufferSize) {
                counterOffset = potentialCounterOffset;
            }
            else {
                // Otherwise, expand the buffer to fit the 4096-aligned counter offset
                bufferSize = ((potentialCounterOffset + sizeof(UINT) + alignment - 1) / alignment) * alignment;
                counterOffset = potentialCounterOffset;
            }

            assert(counterOffset % 4096 == 0);
		}

        auto dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, bufferSize, false, UAV);
        
        //ResourceTransition transition = { dataBuffer.get(), ResourceState::UNKNOWN,  usageType };
//#if defined(_DEBUG)
//        transition.name = L"IndexedStructuredBuffer";
//#endif
//        QueueResourceTransition(transition);

        unsigned int index = m_cbvSrvUavHeap->AllocateDescriptor();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = numElements;
        srvDesc.Buffer.StructureByteStride = elementSize;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_cbvSrvUavHeap->GetCPUHandle(index);
        device->CreateShaderResourceView(dataBuffer->m_buffer.Get(), &srvDesc, srvHandle);

        ShaderVisibleIndexInfo srvInfo;
        srvInfo.index = index;
        srvInfo.gpuHandle = m_cbvSrvUavHeap->GetGPUHandle(index);
        dataBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

        if (UAV) {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Buffer.NumElements = numElements; // We will have some wasted memory to allow the counter to be 4096-aligned
			uavDesc.Buffer.StructureByteStride = elementSize;
			uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            if (UAVCounter) {
                uavDesc.Buffer.CounterOffsetInBytes = counterOffset;
			}
			else {
				uavDesc.Buffer.CounterOffsetInBytes = 0;
			}

			// Shader visible UAV
			unsigned int uavShaderVisibleIndex = m_cbvSrvUavHeap->AllocateDescriptor();
			D3D12_CPU_DESCRIPTOR_HANDLE uavShaderVisibleHandle = m_cbvSrvUavHeap->GetCPUHandle(uavShaderVisibleIndex);
			device->CreateUnorderedAccessView(dataBuffer->m_buffer.Get(), dataBuffer->m_buffer.Get(), &uavDesc, uavShaderVisibleHandle);

			// Non-shader visible UAV
            /*D3D12_UNORDERED_ACCESS_VIEW_DESC uavUintDesc = {};
            uavUintDesc.Format = DXGI_FORMAT_R32_UINT;
            uavUintDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavUintDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
			uavUintDesc.Buffer.NumElements = (numElements*sizeof(T))/sizeof(unsigned int);
			uavUintDesc.Buffer.CounterOffsetInBytes = 0;
			unsigned int uavNonShaderVisibleIndex = m_nonShaderVisibleHeap->AllocateDescriptor();
			D3D12_CPU_DESCRIPTOR_HANDLE uavNonShaderVisibleHandle = m_nonShaderVisibleHeap->GetCPUHandle(uavNonShaderVisibleIndex);
			device->CreateUnorderedAccessView(handle.dataBuffer->m_buffer.Get(), nullptr, &uavUintDesc, uavNonShaderVisibleHandle);*/


			ShaderVisibleIndexInfo uavInfo;
			uavInfo.index = uavShaderVisibleIndex;
			uavInfo.gpuHandle = m_cbvSrvUavHeap->GetGPUHandle(uavShaderVisibleIndex);
            dataBuffer->SetUAVGPUDescriptors(m_cbvSrvUavHeap, {{uavInfo}}, counterOffset);

			//NonShaderVisibleIndexInfo uavNonShaderVisibleInfo;
			//uavNonShaderVisibleInfo.index = uavNonShaderVisibleIndex;
			//uavNonShaderVisibleInfo.cpuHandle = uavNonShaderVisibleHandle;
			//handle.dataBuffer->SetUAVCPUDescriptor(m_nonShaderVisibleHeap, uavNonShaderVisibleInfo);
        }

        return dataBuffer;
    }

    template<typename T>
    std::shared_ptr<DynamicStructuredBuffer<T>> CreateIndexedDynamicStructuredBuffer(UINT capacity = 64, std::wstring name = "", bool UAV = false) {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for structured buffers.");

        auto& device = DeviceManager::GetInstance().GetDevice();

        // Create the dynamic structured buffer instance
        UINT bufferID = GetNextResizableBufferID();
        std::shared_ptr<DynamicStructuredBuffer<T>> pDynamicBuffer = DynamicStructuredBuffer<T>::CreateShared(bufferID, capacity, name, UAV);
//        ResourceTransition transition;
//        transition.resource = pDynamicBuffer.get();
//		transition.beforeState = ResourceState::UNKNOWN;
//		transition.afterState = usage;
//#if defined(_DEBUG)
//        transition.name = name;
//#endif
//		QueueResourceTransition(transition);
        pDynamicBuffer->SetOnResized([this](UINT bufferID, UINT typeSize, UINT capacity, DynamicBufferBase* buffer) {
            this->onDynamicStructuredBufferResized(bufferID, typeSize, capacity, buffer, false);
            });

        // Create an SRV for the buffer
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = capacity;
        srvDesc.Buffer.StructureByteStride = sizeof(T);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
        bufferIDDescriptorIndexMap[bufferID] = index;
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_cbvSrvUavHeap->GetCPUHandle(index);
        device->CreateShaderResourceView(pDynamicBuffer->GetBuffer()->m_buffer.Get(), &srvDesc, cpuHandle);

        ShaderVisibleIndexInfo srvInfo;
        srvInfo.index = index;
        srvInfo.gpuHandle = m_cbvSrvUavHeap->GetGPUHandle(index);
        pDynamicBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

        return pDynamicBuffer;
    }

    template<typename T>
    std::shared_ptr<LazyDynamicStructuredBuffer<T>> CreateIndexedLazyDynamicStructuredBuffer(UINT capacity = 64, std::wstring name = "", size_t alignment = 1, bool UAV = false) {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for structured buffers.");

        auto& device = DeviceManager::GetInstance().GetDevice();

        // Create the dynamic structured buffer instance
        UINT bufferID = GetNextResizableBufferID();
        std::shared_ptr<LazyDynamicStructuredBuffer<T>> pDynamicBuffer = LazyDynamicStructuredBuffer<T>::CreateShared(bufferID, capacity, name, alignment, UAV);
//        ResourceTransition transition;
//        transition.resource = pDynamicBuffer.get();
//        transition.beforeState = ResourceState::UNKNOWN;
//        transition.afterState = usage;
//#if defined(_DEBUG)
//        transition.name = L"LazyDynamicStructuredBuffer";
//#endif
//        QueueResourceTransition(transition);
        pDynamicBuffer->SetOnResized([this](UINT bufferID, UINT typeSize, UINT capacity, DynamicBufferBase* buffer, bool uav) {
            this->onDynamicStructuredBufferResized(bufferID, typeSize, capacity, buffer, uav);
            });

        // Create an SRV for the buffer
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = capacity;
        srvDesc.Buffer.StructureByteStride = sizeof(T);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
        bufferIDDescriptorIndexMap[bufferID] = index;
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_cbvSrvUavHeap->GetCPUHandle(index);
        device->CreateShaderResourceView(pDynamicBuffer->GetBuffer()->m_buffer.Get(), &srvDesc, cpuHandle);

		ShaderVisibleIndexInfo srvInfo;
		srvInfo.index = index;
		srvInfo.gpuHandle = m_cbvSrvUavHeap->GetGPUHandle(index);
        pDynamicBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

        if (UAV) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = capacity;
            uavDesc.Buffer.StructureByteStride = sizeof(T);
            uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            uavDesc.Buffer.CounterOffsetInBytes = 0;

            // Shader visible UAV
            unsigned int uavShaderVisibleIndex = m_cbvSrvUavHeap->AllocateDescriptor();
            D3D12_CPU_DESCRIPTOR_HANDLE uavShaderVisibleHandle = m_cbvSrvUavHeap->GetCPUHandle(uavShaderVisibleIndex);
            device->CreateUnorderedAccessView(pDynamicBuffer->GetAPIResource(), nullptr, &uavDesc, uavShaderVisibleHandle);

            ShaderVisibleIndexInfo uavInfo;
            uavInfo.index = uavShaderVisibleIndex;
            uavInfo.gpuHandle = m_cbvSrvUavHeap->GetGPUHandle(uavShaderVisibleIndex);
            pDynamicBuffer->SetUAVGPUDescriptors(m_cbvSrvUavHeap, {{uavInfo}}, 0);
        }

        return pDynamicBuffer;
    }

    std::shared_ptr<DynamicBuffer> CreateIndexedDynamicBuffer(size_t elementSize, size_t numElements, std::wstring name, bool byteAddress = false, bool UAV = false);
	std::shared_ptr<SortedUnsignedIntBuffer> CreateIndexedSortedUnsignedIntBuffer(UINT capacity, std::wstring name = L"");

    UINT GetNextResizableBufferID() {
        UINT val = numResizableBuffers;
        numResizableBuffers++;
        return val;
    }

    void onDynamicStructuredBufferResized(UINT bufferID, UINT typeSize, UINT capacity, DynamicBufferBase* buffer, bool UAV) {
        UINT descriptorIndex = bufferIDDescriptorIndexMap[bufferID];
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_cbvSrvUavHeap->GetCPUHandle(descriptorIndex);
        auto& device = DeviceManager::GetInstance().GetDevice();

        // Create a Shader Resource View for the buffer
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = capacity;
        srvDesc.Buffer.StructureByteStride = typeSize;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        device->CreateShaderResourceView(buffer->GetAPIResource(), &srvDesc, srvHandle);

        if (UAV){
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = capacity;
            uavDesc.Buffer.StructureByteStride = typeSize;
            uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            uavDesc.Buffer.CounterOffsetInBytes = 0;

            // Shader visible UAV
            D3D12_CPU_DESCRIPTOR_HANDLE uavShaderVisibleHandle = m_cbvSrvUavHeap->GetCPUHandle(buffer->GetUAVShaderVisibleInfo(0).index);

            device->CreateUnorderedAccessView(buffer->GetAPIResource() , nullptr, &uavDesc, uavShaderVisibleHandle);
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

    void onDynamicBufferResized(UINT bufferID, UINT elementSize, UINT numElements, bool byteAddress, DynamicBufferBase* buffer, bool UAV) {
        UINT descriptorIndex = bufferIDDescriptorIndexMap[bufferID];
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_cbvSrvUavHeap->GetCPUHandle(descriptorIndex);
        auto& device = DeviceManager::GetInstance().GetDevice();

        // Create a Shader Resource View for the buffer
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = byteAddress ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = byteAddress ? numElements / 4 : numElements;
        srvDesc.Buffer.StructureByteStride = byteAddress ? 0 : elementSize;
        srvDesc.Buffer.Flags = byteAddress ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;

        device->CreateShaderResourceView(buffer->GetAPIResource(), &srvDesc, srvHandle);

        if (UAV) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.Format = byteAddress ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = byteAddress ? numElements / 4 : numElements;
            uavDesc.Buffer.StructureByteStride = byteAddress ? 0 : elementSize;
            uavDesc.Buffer.CounterOffsetInBytes = 0;
            uavDesc.Buffer.Flags = byteAddress ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;

            // Shader visible UAV
            D3D12_CPU_DESCRIPTOR_HANDLE uavShaderVisibleHandle = m_cbvSrvUavHeap->GetCPUHandle(buffer->GetUAVShaderVisibleInfo(0).index);

            device->CreateUnorderedAccessView(buffer->GetAPIResource(), nullptr, &uavDesc, uavShaderVisibleHandle);
        }

//        auto bufferState = buffer->GetState();
//        // After resize, internal buffer state will not match the wrapper state
//        if (bufferState != ResourceState::UNKNOWN) {
//            ResourceTransition transition;
//            transition.resource = buffer->m_dataBuffer.get();
//            transition.beforeState = ResourceState::UNKNOWN;
//            transition.afterState = buffer->GetState();
//#if defined(_DEBUG)
//            transition.name = buffer->GetName()+L": Resize";
//#endif
//            //QueueResourceTransition(transition);
//        }
    }

    template<typename T>
    std::shared_ptr<Buffer> CreateConstantBuffer(std::wstring name = L"") {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for constant buffers.");

		auto& device = DeviceManager::GetInstance().GetDevice();

		// Calculate the size of the buffer to be 256-byte aligned
		UINT bufferSize = (sizeof(T) + 255) & ~255;

        auto dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, bufferSize, false, false);
		dataBuffer->SetName(name);
//        ResourceTransition transition;
//        transition.resource = dataBuffer.get();
//        transition.beforeState = ResourceState::UNKNOWN;
//        transition.afterState = ResourceState::CONSTANT;
//#if defined(_DEBUG)
//        transition.name = name;
//#endif
//        QueueResourceTransition(transition);


        return dataBuffer;
    }

    std::shared_ptr<Buffer> CreateBuffer(size_t size, void* pInitialData, bool UAV = false);

    std::pair<ComPtr<ID3D12Resource>,ComPtr<ID3D12Heap>> CreateTextureResource(
        const TextureDescription& desc,
        ComPtr<ID3D12Heap> placedResourceHeap = nullptr);

    void UploadTextureData(ID3D12Resource* pResource, const TextureDescription& desc, const std::vector<const stbi_uc*>& initialData, unsigned int arraySize, unsigned int mipLevels);

    UINT CreateIndexedSampler(const D3D12_SAMPLER_DESC& samplerDesc);
    D3D12_CPU_DESCRIPTOR_HANDLE getSamplerCPUHandle(UINT index) const;

	//void QueueResourceTransition(const ResourceTransition& transition);
    //void ExecuteResourceTransitions();

	void SetActiveEnvironmentIndex(unsigned int index) { perFrameCBData.activeEnvironmentIndex = index; }
	void SetOutputType(unsigned int type) { perFrameCBData.outputType = type; }
    void SetTonemapType(unsigned int type) { perFrameCBData.tonemapType = type; }

	ID3D12Resource* GetUAVCounterReset() { return m_uavCounterReset.Get(); }

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
	void SetTransitionCommandQueue(ID3D12CommandQueue* commandQueue);
    void GetCopyCommandList(ComPtr<ID3D12GraphicsCommandList>& commandList, ComPtr<ID3D12CommandAllocator>& commandAllocator);
    void GetDirectCommandList(ComPtr<ID3D12GraphicsCommandList>& commandList, ComPtr<ID3D12CommandAllocator>& commandAllocator);
    void ExecuteAndWaitForCommandList(ComPtr<ID3D12GraphicsCommandList>& commandList, ComPtr<ID3D12CommandAllocator>& commandAllocator);

    std::shared_ptr<DescriptorHeap> m_cbvSrvUavHeap;
    std::shared_ptr<DescriptorHeap> m_samplerHeap;
    std::shared_ptr<DescriptorHeap> m_rtvHeap;
    std::shared_ptr<DescriptorHeap> m_dsvHeap;
    std::shared_ptr<DescriptorHeap> m_nonShaderVisibleHeap;
    UINT numResizableBuffers;
    std::unordered_map<UINT, UINT> bufferIDDescriptorIndexMap;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> copyCommandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> copyCommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> copyCommandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> copyFence;
    HANDLE copyFenceEvent;
    UINT64 copyFenceValue = 0;

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> transitionCommandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> transitionCommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> transitionCommandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> transitionFence;
    HANDLE transitionFenceEvent;
    UINT64 transitionFenceValue = 0;

    std::shared_ptr<Buffer> perFrameBufferHandle;
    UINT8* pPerFrameConstantBuffer;
    PerFrameCB perFrameCBData;
    UINT currentFrameIndex;

    //std::shared_ptr<DynamicStructuredBuffer<LightInfo>> lightBufferPtr;

    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12CommandAllocator> commandAllocator;

    std::vector<std::shared_ptr<Buffer>> buffersToUpdate;
    std::vector<DynamicBufferBase*> dynamicBuffersToUpdate;
	std::vector<ViewedDynamicBufferBase*> dynamicBuffersToUpdateViews;

	//std::vector<ResourceTransition> queuedResourceTransitions;

    ComPtr<ID3D12Resource> m_uavCounterReset;

	int defaultShadowSamplerIndex = -1;

};