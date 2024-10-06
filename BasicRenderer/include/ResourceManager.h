#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <stdexcept>
#include <queue>
#include "DirectX/d3dx12.h"
#include "Buffers.h"
#include "spdlog/spdlog.h"
#include "DynamicStructuredBuffer.h"
#include "ResourceHandles.h"
#include "PixelBuffer.h"
#include "Buffer.h"
#include "DescriptorHeap.h"
#include "ResourceStates.h"
#include "RenderContext.h"
using namespace Microsoft::WRL;

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
    void UpdatePerFrameBuffer(DirectX::XMFLOAT3 eyeWorld, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projectionMatrix, UINT numLights, UINT lightBufferIndex, UINT pointCubemapMatricesBufferIndex, UINT spotMatricesBufferIndex, UINT directionalCascadeMatricesBufferIndex);

    template<typename T>
    BufferHandle CreateIndexedConstantBuffer(std::wstring name = L"") {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for constant buffers.");

        auto& device = DeviceManager::GetInstance().GetDevice();

        // Calculate the size of the buffer to be 256-byte aligned
        UINT bufferSize = (sizeof(T) + 255) & ~255;

        BufferHandle bufferHandle;
        // Create the buffer
        bufferHandle.uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, bufferSize, true);
        bufferHandle.dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, bufferSize, false);
		bufferHandle.dataBuffer->SetName(name);
        ResourceTransition transition;
		transition.resource = bufferHandle.dataBuffer.get();
		transition.beforeState = ResourceState::UNKNOWN;
		transition.afterState = ResourceState::CONSTANT;
		QueueResourceTransition(transition);
        // Create a descriptor for the buffer
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = bufferHandle.dataBuffer->m_buffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = bufferSize;

        UINT index = m_cbvSrvUavHeap->AllocateDescriptor();
        D3D12_CPU_DESCRIPTOR_HANDLE handle = m_cbvSrvUavHeap->GetCPUHandle(index);
        bufferHandle.index = index;

        device->CreateConstantBufferView(&cbvDesc, handle);

        return bufferHandle;
    }

    template<typename T>
    void UpdateConstantBuffer(BufferHandle& handle, const T& data) {
        void* mappedData;
        D3D12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
        handle.uploadBuffer->m_buffer->Map(0, &readRange, &mappedData);
        memcpy(mappedData, &data, sizeof(T));
        handle.uploadBuffer->m_buffer->Unmap(0, nullptr);

        buffersToUpdate.push_back(handle);
    }

    template<typename T>
    BufferHandle CreateIndexedStructuredBuffer(UINT numElements, ResourceState usageType) {
        auto& device = DeviceManager::GetInstance().GetDevice();
        UINT elementSize = sizeof(T);
        UINT bufferSize = numElements * elementSize;

        BufferHandle handle;
        handle.uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, bufferSize, true);
        handle.dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, bufferSize, false);
        
        ResourceTransition transition = { handle.dataBuffer.get(), ResourceState::UNKNOWN,  usageType };
        QueueResourceTransition(transition);

        handle.index = m_cbvSrvUavHeap->AllocateDescriptor();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = numElements;
        srvDesc.Buffer.StructureByteStride = sizeof(T);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_cbvSrvUavHeap->GetCPUHandle(handle.index);
        device->CreateShaderResourceView(handle.dataBuffer->m_buffer.Get(), &srvDesc, srvHandle);

        return handle;
    }

    template<typename T>
    void UpdateIndexedStructuredBuffer(BufferHandle& handle, T* data, UINT startIndex, UINT numElements) {
        //if (handle.buffer == nullptr) {
        //    spdlog::error("Buffer not initialized.");
        //    throw std::runtime_error("Buffer not initialized.");
        //}

        // Calculate the size of the data to update
        UINT elementSize = sizeof(T);
        UINT updateSize = numElements * elementSize;
        UINT offset = startIndex * elementSize;

        // Map the buffer
        void* mappedData = nullptr;
        D3D12_RANGE readRange = { offset, offset + updateSize };
        HRESULT hr = handle.uploadBuffer->m_buffer->Map(0, &readRange, &mappedData);
        if (FAILED(hr)) {
            spdlog::error("Failed to map buffer with HRESULT: {}", hr);
            throw std::runtime_error("Failed to map buffer.");
        }

        // Copy new data to the buffer
        memcpy(static_cast<char*>(mappedData) + offset, data, updateSize);

        // Unmap the buffer
        D3D12_RANGE writtenRange = { offset, offset + updateSize };
        handle.uploadBuffer->m_buffer->Unmap(0, &writtenRange);

        buffersToUpdate.push_back(handle);
    }

    template<typename T>
    DynamicBufferHandle<T> CreateIndexedDynamicStructuredBuffer(ResourceState usage, UINT capacity = 64, std::wstring name = "") {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for structured buffers.");

        auto& device = DeviceManager::GetInstance().GetDevice();

        // Create the dynamic structured buffer instance
        UINT bufferID = GetNextResizableBufferID();
        std::shared_ptr<DynamicStructuredBuffer<T>> pDynamicBuffer = DynamicStructuredBuffer<T>::CreateShared(bufferID, capacity, name);
        ResourceTransition transition;
        transition.resource = pDynamicBuffer.get();
		transition.beforeState = ResourceState::UNKNOWN;
		transition.afterState = usage;
		QueueResourceTransition(transition);
        pDynamicBuffer->SetOnResized([this](UINT bufferID, UINT typeSize, UINT capacity, std::shared_ptr<Buffer>& buffer) {
            this->onBufferResized(bufferID, typeSize, capacity, buffer);
            });

        // Create a Shader Resource View (SRV) for the buffer
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

        DynamicBufferHandle<T> handle;
        handle.index = index;
        handle.buffer = pDynamicBuffer;
        return handle;
    }

    UINT GetNextResizableBufferID() {
        UINT val = numResizableBuffers;
        numResizableBuffers++;
        return val;
    }

    void onBufferResized(UINT bufferID, UINT typeSize, UINT capacity, std::shared_ptr<Buffer>& buffer) {
        UINT descriptorIndex = bufferIDDescriptorIndexMap[bufferID];
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_cbvSrvUavHeap->GetCPUHandle(descriptorIndex);
        auto& device = DeviceManager::GetInstance().GetDevice();

        // Create a Shader Resource View for the buffer
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = capacity;
        srvDesc.Buffer.StructureByteStride = typeSize;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        device->CreateShaderResourceView(buffer->m_buffer.Get(), &srvDesc, srvHandle);
        
		auto bufferState = buffer->GetState();
		// After resize, internal buffer state will not match the wrapper state
		if (bufferState != ResourceState::UNKNOWN) {
			ResourceTransition transition;
			transition.resource = buffer.get();
			transition.beforeState = ResourceState::UNKNOWN;
			transition.afterState = buffer->GetState();
			QueueResourceTransition(transition);
		}
    }

    template<typename T>
    BufferHandle CreateConstantBuffer(std::wstring name = L"") {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for constant buffers.");

		auto& device = DeviceManager::GetInstance().GetDevice();

		// Calculate the size of the buffer to be 256-byte aligned
		UINT bufferSize = (sizeof(T) + 255) & ~255;

        BufferHandle handle;

		// Create the upload and data buffers
        handle.uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, bufferSize, true);
        handle.dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, bufferSize, false);
		handle.dataBuffer->SetName(name);
        ResourceTransition transition;
        transition.resource = handle.dataBuffer.get();
        transition.beforeState = ResourceState::UNKNOWN;
        transition.afterState = ResourceState::CONSTANT;
        QueueResourceTransition(transition);


        return handle;
    }

    TextureHandle<PixelBuffer> CreateTextureFromImage(const stbi_uc* image, int width, int height, int channels, bool sRGB, DXGI_FORMAT textureFormat);
    TextureHandle<PixelBuffer> CreateCubemapFromImages(const std::array<const stbi_uc*, 6>& images, int width, int height, int channels, bool sRGB, DXGI_FORMAT textureFormat);
    TextureHandle<PixelBuffer> CreateTextureArray(int width, int height, int channels, uint32_t length, bool isCubemap, DXGI_FORMAT textureFormat, bool RTV = false, bool DSV = false, bool UAV = false, ResourceState initialState = ResourceState::UNKNOWN);
    TextureHandle<PixelBuffer> CreateTexture(int width, int height, int channels, DXGI_FORMAT textureFormat, bool isCubemap = false, bool RTV = false, bool DSV = false, bool UAV = false, bool mipmap = false, ResourceState initialState = ResourceState::UNKNOWN);

	BufferHandle CreateBuffer(size_t size, ResourceState usageType, void* pInitialData);
	void UpdateBuffer(BufferHandle& handle, void* data, size_t size);
	template<typename T>
    void QueueDynamicBufferUpdate(DynamicBufferHandle<T>& handle) {
        dynamicBuffersToUpdate.push_back(handle.buffer);
    }

    UINT CreateIndexedSampler(const D3D12_SAMPLER_DESC& samplerDesc);
    D3D12_CPU_DESCRIPTOR_HANDLE getSamplerCPUHandle(UINT index) const;
    void UpdateGPUBuffers();

	void QueueResourceTransition(const ResourceTransition& transition);
    void ExecuteResourceTransitions();

	void CreateRenderTargetViewForExternalResource(ID3D12Resource* resource, D3D12_RENDER_TARGET_VIEW_DESC* rtvDesc);

    void setEnvironmentIrradianceMapSamplerIndex(int index) { perFrameCBData.environmentIrradianceSamplerIndex = index; }
	void setEnvironmentIrradianceMapIndex(int index) { perFrameCBData.environmentIrradianceMapIndex = index; }
	void setPrefilteredEnvironmentMapIndex(int index) { perFrameCBData.prefilteredEnvironmentMapIndex = index; }
	void setPrefilteredEnvironmentMapSamplerIndex(int index) { perFrameCBData.prefilteredEnvironmentMapSamplerIndex = index; }
private:
    ResourceManager(){};
    void WaitForCopyQueue();
    void WaitForTransitionQueue();
    void InitializeCopyCommandQueue();
    void InitializeTransitionCommandList();
	void SetTransitionCommandQueue(ID3D12CommandQueue* commandQueue);
    //UINT AllocateDescriptor();
    //void ReleaseDescriptor(UINT index);
    void GetCopyCommandList(ComPtr<ID3D12GraphicsCommandList>& commandList, ComPtr<ID3D12CommandAllocator>& commandAllocator);
    void GetDirectCommandList(ComPtr<ID3D12GraphicsCommandList>& commandList, ComPtr<ID3D12CommandAllocator>& commandAllocator);
    void ExecuteAndWaitForCommandList(ComPtr<ID3D12GraphicsCommandList>& commandList, ComPtr<ID3D12CommandAllocator>& commandAllocator);

    std::unique_ptr<DescriptorHeap> m_cbvSrvUavHeap;
    std::unique_ptr<DescriptorHeap> m_samplerHeap;
    std::unique_ptr<DescriptorHeap> m_rtvHeap;
    std::unique_ptr<DescriptorHeap> m_dsvHeap;
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

	BufferHandle perFrameBufferHandle;
    UINT8* pPerFrameConstantBuffer;
    PerFrameCB perFrameCBData;
    UINT currentFrameIndex;

    DynamicBufferHandle<LightInfo> lightBufferHandle;

    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12CommandAllocator> commandAllocator;

    std::vector<BufferHandle> buffersToUpdate;
    std::vector<std::shared_ptr<DynamicBufferBase>> dynamicBuffersToUpdate;

	std::vector<ResourceTransition> queuedResourceTransitions;

	int defaultShadowSamplerIndex = -1;
};