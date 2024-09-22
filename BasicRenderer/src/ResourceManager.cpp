#include "ResourceManager.h"
#include "Utilities.h"
#include "DirectX/d3dx12.h"
#include "DeviceManager.h"
#include "DynamicStructuredBuffer.h"
#include "SettingsManager.h"

void ResourceManager::Initialize() {
    //for (int i = 0; i < 3; i++) {
    //    frameResourceCopies[i] = std::make_unique<FrameResource>();
    //    frameResourceCopies[i]->Initialize();
    //}

    auto& device = DeviceManager::GetInstance().GetDevice();
    m_cbvSrvUavHeap = std::make_unique<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1, true);
    m_samplerHeap = std::make_unique<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true);
    m_rtvHeap = std::make_unique<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 10000, false);
    m_dsvHeap = std::make_unique<DescriptorHeap>(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 10000, false);

    UINT perFrameBufferSize = (sizeof(PerFrameCB) + 255) & ~255; // CBV size is required to be 256-byte aligned.
    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(perFrameBufferSize);

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&perFrameConstantBuffer)));

    perFrameCBData.ambientLighting = XMVectorSet(0.1, 0.1, 0.1, 1.0);
    perFrameCBData.numShadowCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades")();
    auto shadowCascadeSplits = SettingsManager::GetInstance().getSettingGetter<std::vector<float>>("directionalLightCascadeSplits")();
    switch (perFrameCBData.numShadowCascades) {
	case 1:
        perFrameCBData.shadowCascadeSplits = XMVectorSet(shadowCascadeSplits[0], 0, 0, 0);
		break;
	case 2:
		perFrameCBData.shadowCascadeSplits = XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], 0, 0);
		break;
	case 3:
		perFrameCBData.shadowCascadeSplits = XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], shadowCascadeSplits[2], 0);
		break;
	case 4:
		perFrameCBData.shadowCascadeSplits = XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], shadowCascadeSplits[2], shadowCascadeSplits[3]);
    }

    // Map the constant buffer and initialize it

    InitializeUploadHeap();
    InitializeCopyCommandQueue();
	InitializeTransitionCommandQueue();

    // Create CBV
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = perFrameConstantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = perFrameBufferSize; // CBV size is required to be 256-byte aligned.
    unsigned int lightsIndex = m_cbvSrvUavHeap->AllocateDescriptor();
    device->CreateConstantBufferView(&cbvDesc, m_cbvSrvUavHeap->GetCPUHandle(lightsIndex));
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ResourceManager::GetSRVCPUHandle(UINT index) {
	return m_cbvSrvUavHeap->GetCPUHandle(index);
}

CD3DX12_GPU_DESCRIPTOR_HANDLE ResourceManager::GetSRVGPUHandle(UINT index) {
	return m_cbvSrvUavHeap->GetGPUHandle(index);
}

ComPtr<ID3D12DescriptorHeap> ResourceManager::GetSRVDescriptorHeap() {
    return m_cbvSrvUavHeap->GetHeap();
}

ComPtr<ID3D12DescriptorHeap> ResourceManager::GetSamplerDescriptorHeap() {
    return m_samplerHeap->GetHeap();
}

//UINT ResourceManager::AllocateDescriptor() {
//    if (!freeDescriptors.empty()) {
//        UINT freeIndex = freeDescriptors.front();
//        freeDescriptors.pop();
//        return freeIndex;
//    }
//    else {
//        if (numAllocatedDescriptors >= descriptorHeap->GetDesc().NumDescriptors) {
//            throw std::runtime_error("Out of descriptor heap space!");
//        }
//        return numAllocatedDescriptors++;
//    }
//}
//
//void ResourceManager::ReleaseDescriptor(UINT index) {
//    freeDescriptors.push(index);
//}


void ResourceManager::UpdateConstantBuffers(DirectX::XMFLOAT3 eyeWorld, DirectX::XMMATRIX viewMatrix, DirectX::XMMATRIX projectionMatrix, UINT numLights, UINT lightBufferIndex, UINT pointCubemapMatricesBufferIndex, UINT spotMatricesBufferIndex, UINT directionalCascadeMatricesBufferIndex) {
    //DirectX::XMFLOAT4 eyeWorld = { 0.0f, 2.0f, -5.0f, 1.0f };
    //perFrameCBData.view = DirectX::XMMatrixLookAtLH(
    //    DirectX::XMLoadFloat4(&eyeWorld), // Eye position
    //    DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f),  // Focus point
    //    DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)   // Up direction
    //);
    perFrameCBData.viewMatrix = viewMatrix;
    //perFrameCBData.projection = DirectX::XMMatrixPerspectiveFovLH(
    //    DirectX::XM_PIDIV2, // Field of View
    //    800.0f / 600.0f,    // Aspect ratio
    //    0.1f,               // Near clipping plane
    //    100.0f              // Far clipping plane
    //);
    perFrameCBData.projectionMatrix = projectionMatrix;
    perFrameCBData.eyePosWorldSpace = DirectX::XMLoadFloat3(&eyeWorld);
    perFrameCBData.numLights = numLights;
    perFrameCBData.lightBufferIndex = lightBufferIndex;
	perFrameCBData.pointLightCubemapBufferIndex = pointCubemapMatricesBufferIndex;
	perFrameCBData.spotLightMatrixBufferIndex = spotMatricesBufferIndex;
	perFrameCBData.directionalLightCascadeBufferIndex = directionalCascadeMatricesBufferIndex;
    // Map the upload heap and copy new data to it
    void* pUploadData;
    D3D12_RANGE readRange(0, 0);
    ThrowIfFailed(uploadHeap->Map(0, &readRange, &pUploadData));
    memcpy(pUploadData, &perFrameCBData, sizeof(perFrameCBData));
    uploadHeap->Unmap(0, nullptr);

    // Reset and record the copy command list
    ThrowIfFailed(copyCommandAllocator->Reset());
    ThrowIfFailed(copyCommandList->Reset(copyCommandAllocator.Get(), nullptr));
    copyCommandList->CopyBufferRegion(perFrameConstantBuffer.Get(), 0, uploadHeap.Get(), 0, sizeof(perFrameCBData));
    ThrowIfFailed(copyCommandList->Close());

    // Execute the copy command list
    ID3D12CommandList* ppCommandLists[] = { copyCommandList.Get() };
    copyCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Wait for the copy queue to finish
    WaitForCopyQueue();

    // TODO: Replace above with new buffer management system used elsewhere
}

void ResourceManager::InitializeUploadHeap() {
    auto& device = DeviceManager::GetInstance().GetDevice();
    D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer((sizeof(PerFrameCB) + 255) & ~255);
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadHeap)));
}

void ResourceManager::WaitForCopyQueue() {
    ThrowIfFailed(copyCommandQueue->Signal(copyFence.Get(), ++copyFenceValue));
    if (copyFence->GetCompletedValue() < copyFenceValue) {
        ThrowIfFailed(copyFence->SetEventOnCompletion(copyFenceValue, copyFenceEvent));
        WaitForSingleObject(copyFenceEvent, INFINITE);
    }
}

void ResourceManager::WaitForTransitionQueue() {
    ThrowIfFailed(transitionCommandQueue->Signal(transitionFence.Get(), ++transitionFenceValue));
    if (transitionFence->GetCompletedValue() < transitionFenceValue) {
        ThrowIfFailed(transitionFence->SetEventOnCompletion(transitionFenceValue, transitionFenceEvent));
        WaitForSingleObject(transitionFenceEvent, INFINITE);
    }
}

void ResourceManager::InitializeCopyCommandQueue() {
    auto& device = DeviceManager::GetInstance().GetDevice();

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&copyCommandQueue)));

    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&copyCommandAllocator)));
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, copyCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&copyCommandList)));
    copyCommandList->Close();

    ThrowIfFailed(device->CreateFence(copyFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copyFence)));
    copyFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (copyFenceEvent == nullptr) {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
}

void ResourceManager::InitializeTransitionCommandQueue() {
    auto& device = DeviceManager::GetInstance().GetDevice();

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&transitionCommandQueue)));

    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&transitionCommandAllocator)));
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, transitionCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&transitionCommandList)));
    transitionCommandList->Close();

    ThrowIfFailed(device->CreateFence(transitionFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&transitionFence)));
    transitionFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (transitionFenceEvent == nullptr) {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
}

UINT ResourceManager::CreateIndexedSampler(const D3D12_SAMPLER_DESC& samplerDesc) {
    auto& device = DeviceManager::GetInstance().GetDevice();

	UINT index = m_samplerHeap->AllocateDescriptor();
	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_samplerHeap->GetCPUHandle(index);

    device->CreateSampler(&samplerDesc, handle);

    return index;
}

D3D12_CPU_DESCRIPTOR_HANDLE ResourceManager::getSamplerCPUHandle(UINT index) const {
    return m_samplerHeap->GetCPUHandle(index);
}

TextureHandle<PixelBuffer> ResourceManager::CreateTextureFromImage(const stbi_uc* image, int width, int height, int channels, bool sRGB) {
    auto& device = DeviceManager::GetInstance().GetDevice();

    // Describe and create the texture resource

    DXGI_FORMAT textureFormat;
    std::vector<stbi_uc> expandedImage;
    switch (channels) {
    case 1:
        textureFormat = DXGI_FORMAT_R8_UNORM;
        break;
    case 3:
        textureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        expandedImage.resize(width * height * 4); // 4 channels for RGBA
        for (int i = 0; i < width * height; ++i) {
            expandedImage[i * 4] = image[i * 3];     // R
            expandedImage[i * 4 + 1] = image[i * 3 + 1]; // G
            expandedImage[i * 4 + 2] = image[i * 3 + 2]; // B
            expandedImage[i * 4 + 3] = 255;          // A
        }
        image = expandedImage.data(); // Use expanded data
        channels = 4; // Update the number of channels
        break;
    case 4:
        textureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    default:
        throw std::invalid_argument("Unsupported channel count");
    }

    CD3DX12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(textureFormat, width, height);
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> textureResource;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&textureResource)));

    // Create an upload heap
    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(textureResource.Get(), 0, 1);
    CD3DX12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    D3D12_HEAP_PROPERTIES uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    ComPtr<ID3D12Resource> textureUploadHeap;
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&textureUploadHeap)));

    // Upload the texture data to the GPU
    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = image;
    textureData.RowPitch = width * channels; // Calculate based on the number of channels
    textureData.SlicePitch = textureData.RowPitch * height;

    // Initialize copy command list, used to copy from the upload heap to the default heap
    GetCopyCommandList(commandList, commandAllocator);

    UpdateSubresources(commandList.Get(), textureResource.Get(), textureUploadHeap.Get(), 0, 0, 1, &textureData);

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(textureResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &barrier);

    ExecuteAndWaitForCommandList(commandList, commandAllocator);

    // Allocate descriptor and create shader resource view
    UINT srvDescriptorIndex = m_cbvSrvUavHeap->AllocateDescriptor();
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvCPUHandle = m_cbvSrvUavHeap->GetCPUHandle(srvDescriptorIndex);
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvGPUHandle = m_cbvSrvUavHeap->GetGPUHandle(srvDescriptorIndex);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = textureFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, srvCPUHandle);

	ShaderVisibleIndexInfo SRVInfo;
	SRVInfo.index = srvDescriptorIndex;
	SRVInfo.cpuHandle = srvCPUHandle;
	SRVInfo.gpuHandle = srvGPUHandle;

    TextureHandle<PixelBuffer> handle;
	handle.texture = textureResource;
	handle.SRVInfo = SRVInfo;
    return handle;
}

TextureHandle<PixelBuffer> ResourceManager::CreateTexture(int width, int height, int channels, bool isCubemap, bool RTV, bool DSV, bool UAV, ResourceState initialState) {
    auto& device = DeviceManager::GetInstance().GetDevice();

    // Describe and create the texture resource

    DXGI_FORMAT textureFormat;
    switch (channels) {
    case 1:
        if (DSV) {
            textureFormat = DXGI_FORMAT_R32_TYPELESS;
        }
        else {
            textureFormat = DXGI_FORMAT_R32_FLOAT;
        }
        break;
    case 3:
        textureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        channels = 4; // Update the number of channels to RGBA
        break;
    case 4:
        textureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    default:
        throw std::invalid_argument("Unsupported channel count");
    }

    CD3DX12_RESOURCE_DESC textureDesc;
    if (isCubemap) {
        textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            textureFormat,
            width,
            height,
            6, // For a single cubemap (6 faces)
            1, // Single mip level
            1, // Sample count
            0); // Sample quality
    }
    else {
        textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            textureFormat,
            width,
            height,
            1,  // For a single 2D texture
            1,  // Single mip level
            1,  // Sample count
            0); // Sample quality
    }
    if (DSV) {
        textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    } if (RTV) {
        textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = textureFormat != DXGI_FORMAT_R32_TYPELESS ? textureFormat : DXGI_FORMAT_R32_FLOAT;

    if (isCubemap) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = 1;
        srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    }
    else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    }

    TextureHandle<PixelBuffer> handle;

    // Create the committed resource
    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> textureResource;

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    depthOptimizedClearValue.DepthStencil.Stencil = 0;
	
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R32_FLOAT;
    clearValue.Color[0] = 1.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 0.0f;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COMMON,
        (DSV) ? &depthOptimizedClearValue : (channels == 1) ? &clearValue : nullptr,
        IID_PPV_ARGS(&textureResource)));

    // Allocate descriptor and create shader resource view
    UINT srvDescriptorIndex = m_cbvSrvUavHeap->AllocateDescriptor();
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvCPUHandle = m_cbvSrvUavHeap->GetCPUHandle(srvDescriptorIndex);
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvGPUHandle = m_cbvSrvUavHeap->GetGPUHandle(srvDescriptorIndex);

    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, srvCPUHandle);

    // Create Render Target View if requested
    if (RTV) {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = textureFormat;

        if (isCubemap) {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.ArraySize = 1; // One slice at a time
            rtvDesc.Texture2DArray.MipSlice = 0;
            rtvDesc.Texture2DArray.PlaneSlice = 0;

            // Loop through all 6 cubemap faces
            for (int layerIndex = 0; layerIndex < 6; ++layerIndex) {
                rtvDesc.Texture2DArray.FirstArraySlice = layerIndex; // Set the array slice (cubemap face)

                UINT rtvDescriptorIndex = m_rtvHeap->AllocateDescriptor();
                CD3DX12_CPU_DESCRIPTOR_HANDLE rtvCPUHandle = m_rtvHeap->GetCPUHandle(rtvDescriptorIndex);

                device->CreateRenderTargetView(textureResource.Get(), &rtvDesc, rtvCPUHandle);

                // Store RTV information for this layer
                NonShaderVisibleIndexInfo rtvInfo;
                rtvInfo.index = rtvDescriptorIndex;
                rtvInfo.cpuHandle = rtvCPUHandle;
                handle.RTVInfo.push_back(rtvInfo); // Push to RTVInfo vector
            }
        }
        else {
            // Non-cubemap case
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
            rtvDesc.Texture2D.PlaneSlice = 0;

            UINT rtvDescriptorIndex = m_rtvHeap->AllocateDescriptor();
            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvCPUHandle = m_rtvHeap->GetCPUHandle(rtvDescriptorIndex);

            device->CreateRenderTargetView(textureResource.Get(), &rtvDesc, rtvCPUHandle);

            // Store RTV information
            NonShaderVisibleIndexInfo rtvInfo;
            rtvInfo.index = rtvDescriptorIndex;
            rtvInfo.cpuHandle = rtvCPUHandle;
            handle.RTVInfo.push_back(rtvInfo); // Push to RTVInfo vector
        }
    }

	// Create Depth Stencil View if requested
	if (DSV) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;

        if (isCubemap) {
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsvDesc.Texture2DArray.ArraySize = 1; // One slice at a time
            dsvDesc.Texture2DArray.MipSlice = 0;

            // Loop through all 6 cubemap faces
            for (int layerIndex = 0; layerIndex < 6; ++layerIndex) {
                dsvDesc.Texture2DArray.FirstArraySlice = layerIndex; // Set the array slice (cubemap face)

                UINT dsvDescriptorIndex = m_dsvHeap->AllocateDescriptor();
                CD3DX12_CPU_DESCRIPTOR_HANDLE dsvCPUHandle = m_dsvHeap->GetCPUHandle(dsvDescriptorIndex);

                device->CreateDepthStencilView(textureResource.Get(), &dsvDesc, dsvCPUHandle);

                // Store DSV information for this layer
                NonShaderVisibleIndexInfo dsvInfo;
                dsvInfo.index = dsvDescriptorIndex;
                dsvInfo.cpuHandle = dsvCPUHandle;
                handle.DSVInfo.push_back(dsvInfo); // Push to DSVInfo vector
            }
        }
        else {
            // Non-cubemap case, similar to what you have for 2D textures
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dsvDesc.Texture2D.MipSlice = 0;

            UINT dsvDescriptorIndex = m_dsvHeap->AllocateDescriptor();
            CD3DX12_CPU_DESCRIPTOR_HANDLE dsvCPUHandle = m_dsvHeap->GetCPUHandle(dsvDescriptorIndex);

            device->CreateDepthStencilView(textureResource.Get(), &dsvDesc, dsvCPUHandle);

            // Store DSV information
            NonShaderVisibleIndexInfo dsvInfo;
            dsvInfo.index = dsvDescriptorIndex;
            dsvInfo.cpuHandle = dsvCPUHandle;
            handle.DSVInfo.push_back(dsvInfo); // Push to DSVInfo vector
        }
	}

	ShaderVisibleIndexInfo SRVInfo;
	SRVInfo.index = srvDescriptorIndex;
	SRVInfo.cpuHandle = srvCPUHandle;
	SRVInfo.gpuHandle = srvGPUHandle;

    handle.SRVInfo = SRVInfo;
	handle.texture = textureResource;

    return handle;
}

TextureHandle<PixelBuffer> ResourceManager::CreateTextureArray(int width, int height, int channels, uint32_t length, bool isCubemap, bool RTV, bool DSV, bool UAV, ResourceState initialState) {
    auto& device = DeviceManager::GetInstance().GetDevice();

    // Describe and create the texture resource

    DXGI_FORMAT textureFormat;
    std::vector<stbi_uc> expandedImage;
    switch (channels) {
    case 1:
        if (DSV) {
            textureFormat = DXGI_FORMAT_R32_TYPELESS;
        }
        else {
            textureFormat = DXGI_FORMAT_R32_FLOAT;
        }
        break;
    case 3:
        textureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        channels = 4; // Update the number of channels
        break;
    case 4:
        textureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    default:
        throw std::invalid_argument("Unsupported channel count");
    }

    CD3DX12_RESOURCE_DESC textureDesc;
    if (isCubemap) {
        textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            textureFormat,
            width,
            height,
            6 * length, // For cubemap arrays
            1,
            1,
            0);
    }
    else {
        textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            textureFormat,
            width,
            height,
            length, // For regular texture arrays
            1,
            1,
            0);
    }
    if (DSV) {
        textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    } if (RTV) {
        textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = textureFormat != DXGI_FORMAT_R32_TYPELESS ? textureFormat : DXGI_FORMAT_R32_FLOAT;

    if (isCubemap) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        srvDesc.TextureCubeArray.MostDetailedMip = 0;
        srvDesc.TextureCubeArray.MipLevels = 1;
        srvDesc.TextureCubeArray.First2DArrayFace = 0;
        srvDesc.TextureCubeArray.NumCubes = length;
        srvDesc.TextureCubeArray.ResourceMinLODClamp = 0.0f;
    }
    else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srvDesc.Texture2DArray.MostDetailedMip = 0;
        srvDesc.Texture2DArray.MipLevels = 1;
        srvDesc.Texture2DArray.FirstArraySlice = 0;
        srvDesc.Texture2DArray.ArraySize = length;
        srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
    }

    TextureHandle<PixelBuffer> handle;

    D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> textureResource;

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    depthOptimizedClearValue.DepthStencil.Stencil = 0;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R32_FLOAT;
    clearValue.Color[0] = 1.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 0.0f;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COMMON,
        (DSV) ? &depthOptimizedClearValue : (channels == 1) ? &clearValue : nullptr,
        IID_PPV_ARGS(&textureResource)));

    UINT srvDescriptorIndex = m_cbvSrvUavHeap->AllocateDescriptor();
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvCPUHandle = m_cbvSrvUavHeap->GetCPUHandle(srvDescriptorIndex);
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvGPUHandle = m_cbvSrvUavHeap->GetGPUHandle(srvDescriptorIndex);

    device->CreateShaderResourceView(textureResource.Get(), &srvDesc, srvCPUHandle);

	// Create Render Target View if requested
	NonShaderVisibleIndexInfo RTVInfo;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};
	if (RTV) {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = textureFormat;

        if (isCubemap) {
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.ArraySize = 1; // One slice at a time (6 * length total)
            rtvDesc.Texture2DArray.MipSlice = 0;
            rtvDesc.Texture2DArray.PlaneSlice = 0;

            // Loop through all cubemap array layers
            for (int arrayLayer = 0; arrayLayer < 6 * length; ++arrayLayer) {
                rtvDesc.Texture2DArray.FirstArraySlice = arrayLayer; // Set the array slice (cubemap face + array layer)

                UINT rtvDescriptorIndex = m_rtvHeap->AllocateDescriptor();
                CD3DX12_CPU_DESCRIPTOR_HANDLE rtvCPUHandle = m_rtvHeap->GetCPUHandle(rtvDescriptorIndex);

                device->CreateRenderTargetView(textureResource.Get(), &rtvDesc, rtvCPUHandle);

                // Store RTV information for this array slice
                NonShaderVisibleIndexInfo rtvInfo;
                rtvInfo.index = rtvDescriptorIndex;
                rtvInfo.cpuHandle = rtvCPUHandle;
                handle.RTVInfo.push_back(rtvInfo); // Push to RTVInfo vector
            }
        }
        else {
            // For regular texture arrays
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.ArraySize = 1; // One slice at a time (length total)
            rtvDesc.Texture2DArray.MipSlice = 0;
            rtvDesc.Texture2DArray.PlaneSlice = 0;

            // Loop through all texture array layers
            for (int arrayLayer = 0; arrayLayer < length; ++arrayLayer) {
                rtvDesc.Texture2DArray.FirstArraySlice = arrayLayer; // Set the array slice

                UINT rtvDescriptorIndex = m_rtvHeap->AllocateDescriptor();
                CD3DX12_CPU_DESCRIPTOR_HANDLE rtvCPUHandle = m_rtvHeap->GetCPUHandle(rtvDescriptorIndex);

                device->CreateRenderTargetView(textureResource.Get(), &rtvDesc, rtvCPUHandle);

                // Store RTV information for this array slice
                NonShaderVisibleIndexInfo rtvInfo;
                rtvInfo.index = rtvDescriptorIndex;
                rtvInfo.cpuHandle = rtvCPUHandle;
                handle.RTVInfo.push_back(rtvInfo); // Push to RTVInfo vector
            }
        }
	}

	// Create Depth Stencil View if requested
	NonShaderVisibleIndexInfo DSVInfo;
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
	if (DSV) {
        // Create Depth Stencil Views for each cubemap array layer or regular texture array layer
        if (DSV) {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;

            if (isCubemap) {
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                dsvDesc.Texture2DArray.ArraySize = 1; // One slice at a time (6 * length total)
                dsvDesc.Texture2DArray.MipSlice = 0;

                // Loop through all cubemap array layers
                for (int arrayLayer = 0; arrayLayer < 6 * length; ++arrayLayer) {
                    dsvDesc.Texture2DArray.FirstArraySlice = arrayLayer; // Set the array slice (cubemap face + array layer)

                    UINT dsvDescriptorIndex = m_dsvHeap->AllocateDescriptor();
                    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvCPUHandle = m_dsvHeap->GetCPUHandle(dsvDescriptorIndex);

                    device->CreateDepthStencilView(textureResource.Get(), &dsvDesc, dsvCPUHandle);

                    // Store DSV information for this array slice
                    NonShaderVisibleIndexInfo dsvInfo;
                    dsvInfo.index = dsvDescriptorIndex;
                    dsvInfo.cpuHandle = dsvCPUHandle;
                    handle.DSVInfo.push_back(dsvInfo); // Push to DSVInfo vector
                }
            }
            else {
                // For regular texture arrays
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                dsvDesc.Texture2DArray.ArraySize = 1; // One slice at a time (length total)
                dsvDesc.Texture2DArray.MipSlice = 0;

                // Loop through all texture array layers
                for (int arrayLayer = 0; arrayLayer < length; ++arrayLayer) {
                    dsvDesc.Texture2DArray.FirstArraySlice = arrayLayer; // Set the array slice

                    UINT dsvDescriptorIndex = m_dsvHeap->AllocateDescriptor();
                    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvCPUHandle = m_dsvHeap->GetCPUHandle(dsvDescriptorIndex);

                    device->CreateDepthStencilView(textureResource.Get(), &dsvDesc, dsvCPUHandle);

                    // Store DSV information for this array slice
                    NonShaderVisibleIndexInfo dsvInfo;
                    dsvInfo.index = dsvDescriptorIndex;
                    dsvInfo.cpuHandle = dsvCPUHandle;
                    handle.DSVInfo.push_back(dsvInfo); // Push to DSVInfo vector
                }
            }
        }
	}

	ShaderVisibleIndexInfo SRVInfo;
	SRVInfo.index = srvDescriptorIndex;
	SRVInfo.cpuHandle = srvCPUHandle;
	SRVInfo.gpuHandle = srvGPUHandle;

	handle.texture = textureResource;
	handle.SRVInfo = SRVInfo;


    return handle;
}

void ResourceManager::GetCopyCommandList(ComPtr<ID3D12GraphicsCommandList>& commandList, ComPtr<ID3D12CommandAllocator>& commandAllocator) {
    auto& device = DeviceManager::GetInstance().GetDevice();

    // Create a new command allocator if none is available or reuse an existing one
    if (!commandAllocator || FAILED(commandAllocator->Reset())) {
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
    }

    if (!commandList || FAILED(commandList->Reset(commandAllocator.Get(), nullptr))) {
        ThrowIfFailed(device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList)
        ));
    }
}

void ResourceManager::ExecuteAndWaitForCommandList(ComPtr<ID3D12GraphicsCommandList>& commandList, ComPtr<ID3D12CommandAllocator>& commandAllocator) {
    auto& device = DeviceManager::GetInstance().GetDevice();
    static ComPtr<ID3D12CommandQueue> copyCommandQueue;
    static ComPtr<ID3D12Fence> copyFence;
    static HANDLE copyFenceEvent = nullptr;
    static UINT64 copyFenceValue = 0;

    // Create the command queue if it hasn't been created yet
    if (!copyCommandQueue) {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&copyCommandQueue)));
    }

    // Create a fence for synchronization if it hasn't been created yet
    if (!copyFence) {
        ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copyFence)));
        copyFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!copyFenceEvent) {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
    }

    // Close the command list and execute it
    ThrowIfFailed(commandList->Close());
    ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
    copyCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Increment the fence value and signal the fence
    ++copyFenceValue;
    ThrowIfFailed(copyCommandQueue->Signal(copyFence.Get(), copyFenceValue));

    // Wait until the fence is completed
    if (copyFence->GetCompletedValue() < copyFenceValue) {
        ThrowIfFailed(copyFence->SetEventOnCompletion(copyFenceValue, copyFenceEvent));
        WaitForSingleObject(copyFenceEvent, INFINITE);
    }

    ThrowIfFailed(commandAllocator->Reset());
}

void ResourceManager::UpdateGPUBuffers(){
    if (buffersToUpdate.size() == 0) {
        return;
    }

    // Reset the command allocator
    HRESULT hr = copyCommandAllocator->Reset();
    if (FAILED(hr)) {
        spdlog::error("Failed to reset command allocator");
    }

    hr = copyCommandList->Reset(copyCommandAllocator.Get(), nullptr);
    if (FAILED(hr)) {
        spdlog::error("Failed to reset command list");
    }
    for (BufferHandle& bufferHandle : buffersToUpdate) {
        // Ensure both buffers are valid
        if (bufferHandle.uploadBuffer && bufferHandle.dataBuffer) {
            auto startState = TranslateUsageType(bufferHandle.dataBuffer->m_usageType);
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = bufferHandle.dataBuffer->m_buffer.Get();
            barrier.Transition.StateBefore = startState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            // Transition the data buffer to a state suitable for copying into it
            copyCommandList->ResourceBarrier(1, &barrier);

            // Perform the copy
            copyCommandList->CopyResource(bufferHandle.dataBuffer->m_buffer.Get(), bufferHandle.uploadBuffer->m_buffer.Get());

            // Transition back to the original state
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = startState;
            copyCommandList->ResourceBarrier(1, &barrier);
        }
    }
    for (DynamicBufferBase& dynamicBufferHandle : dynamicBuffersToUpdate) {
		// Ensure both buffers are valid
		if (dynamicBufferHandle.m_uploadBuffer && dynamicBufferHandle.m_dataBuffer) {
            auto startState = TranslateUsageType(dynamicBufferHandle.m_dataBuffer->m_usageType);
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource = dynamicBufferHandle.m_dataBuffer->m_buffer.Get();
			barrier.Transition.StateBefore = startState;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

			// Transition the data buffer to a state suitable for copying into it
			copyCommandList->ResourceBarrier(1, &barrier);

			// Perform the copy
			copyCommandList->CopyResource(dynamicBufferHandle.m_dataBuffer->m_buffer.Get(), dynamicBufferHandle.m_uploadBuffer->m_buffer.Get());

			// Transition back to the original state
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = startState;
			copyCommandList->ResourceBarrier(1, &barrier);
		}
    }
    hr = copyCommandList->Close();
    if (FAILED(hr)) {
        spdlog::error("Failed to close command list");
    }
    // Execute the copy command list
    ID3D12CommandList* ppCommandLists[] = { copyCommandList.Get() };
    copyCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    WaitForCopyQueue();
    buffersToUpdate.clear();
}

BufferHandle ResourceManager::CreateBuffer(size_t bufferSize, ResourceUsageType usageType, void* pInitialData) {
	auto& device = DeviceManager::GetInstance().GetDevice();
	BufferHandle handle;
	handle.uploadBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::WRITE, bufferSize, true);
	handle.dataBuffer = Buffer::CreateShared(device.Get(), ResourceCPUAccessType::NONE, bufferSize, false);
	if (pInitialData) {
		UpdateBuffer(handle, pInitialData, bufferSize);
	}
	D3D12_RESOURCE_STATES state = TranslateUsageType(usageType);

	QueueResourceTransition({ handle.dataBuffer->m_buffer.Get(), D3D12_RESOURCE_STATE_COMMON,  state});
	return handle;
}

void ResourceManager::UpdateBuffer(BufferHandle& bufferHandle, void* pData, size_t size) {
	if (bufferHandle.uploadBuffer && bufferHandle.dataBuffer) {
        void* mappedData;
        D3D12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
        bufferHandle.uploadBuffer->m_buffer->Map(0, &readRange, &mappedData);
        memcpy(mappedData, pData, size);
        bufferHandle.uploadBuffer->m_buffer->Unmap(0, nullptr);
		buffersToUpdate.push_back(bufferHandle);
	}
}

void ResourceManager::QueueResourceTransition(const ResourceTransition& transition) {
	queuedResourceTransitions.push_back(transition);
}

void ResourceManager::ExecuteResourceTransitions() {
	auto& device = DeviceManager::GetInstance().GetDevice();
    auto& commandList = transitionCommandList;
    auto& commandAllocator = transitionCommandAllocator;
    if (queuedResourceTransitions.size() == 0) {
        return;
    }

    // Reset the command allocator
    HRESULT hr = commandAllocator->Reset();
    if (FAILED(hr)) {
        spdlog::error("Failed to reset command allocator");
    }

    hr = commandList->Reset(commandAllocator.Get(), nullptr);
    if (FAILED(hr)) {
        spdlog::error("Failed to reset command list");
    }

	for (auto& transition : queuedResourceTransitions) {
		D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(transition.resource, transition.beforeState, transition.afterState);
		commandList->ResourceBarrier(1, &barrier);
	}

    hr = commandList->Close();
    if (FAILED(hr)) {
        spdlog::error("Failed to close command list");
    }

    ID3D12CommandList* ppCommandLists[] = { transitionCommandList.Get() };
    transitionCommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
    WaitForTransitionQueue();

	queuedResourceTransitions.clear();
}