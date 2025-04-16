#include "Managers/Singletons/ReadbackManager.h"

#include <DirectXTex.h>

#include "Texture.h"
#include "PixelBuffer.h"

std::unique_ptr<ReadbackManager> ReadbackManager::instance = nullptr;
bool ReadbackManager::initialized = false;

void ReadbackManager::SaveCubemapToDDS(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, Texture* cubemap, const std::wstring& outputFile, UINT64 fenceValue) {
    D3D12_RESOURCE_DESC resourceDesc = cubemap->GetBuffer()->GetTexture()->GetDesc();

    // Get the number of mip levels and subresources
    UINT numMipLevels = resourceDesc.MipLevels;
    UINT numSubresources = 6 * numMipLevels;  // 6 faces, each with multiple mip levels

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
    std::vector<UINT> numRows(numSubresources);
    std::vector<UINT64> rowSizesInBytes(numSubresources);
    UINT64 totalSize = 0;

    // Get the copyable footprints for each mip level of each face
    device->GetCopyableFootprints(
        &resourceDesc,
        0,
        numSubresources,
        0,
        layouts.data(),
        numRows.data(),
        rowSizesInBytes.data(),
        &totalSize);

    // Describe and create the readback buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = totalSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;  // Buffers don't have a format
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;  // Buffers should be row-major
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> readbackBuffer;
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer));
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create readback buffer!");
    }
    readbackBuffer->SetName(L"Readback");

    auto initialState = ResourceStateToD3D12(cubemap->GetState());
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(cubemap->GetBuffer()->GetTexture().Get(), initialState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(1, &barrier);

    // Issue copy commands for each mip level of each face
    for (UINT mipLevel = 0; mipLevel < numMipLevels; ++mipLevel) {
        for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex) {
            UINT subresourceIndex = D3D12CalcSubresource(mipLevel, faceIndex, 0, numMipLevels, 6);

            D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
            srcLocation.pResource = cubemap->GetBuffer()->GetTexture().Get();
            srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            srcLocation.SubresourceIndex = subresourceIndex;

            D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
            dstLocation.pResource = readbackBuffer.Get();
            dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dstLocation.PlacedFootprint = layouts[subresourceIndex];

            commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
        }
    }

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(cubemap->GetBuffer()->GetTexture().Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, initialState);
    commandList->ResourceBarrier(1, &barrier);

    ReadbackRequest readbackRequest;
    readbackRequest.readbackBuffer = readbackBuffer;
    readbackRequest.layouts = layouts;
    readbackRequest.totalSize = totalSize;
    readbackRequest.outputFile = outputFile;
    readbackRequest.fenceValue = fenceValue;
    readbackRequest.callback = [=]() {
        std::thread([=] {
            void* mappedData = nullptr;
            D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(totalSize) };
            auto hr = readbackBuffer->Map(0, &readRange, &mappedData);
            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to map the readback buffer!");
            }

            // Create a ScratchImage and fill it with the cubemap and mipmap data
            DirectX::ScratchImage scratchImage;
            scratchImage.InitializeCube(resourceDesc.Format, resourceDesc.Width, resourceDesc.Height, 1, numMipLevels);  // Initialize with mip levels

            for (UINT mipLevel = 0; mipLevel < numMipLevels; ++mipLevel) {
                for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex) {
                    UINT subresourceIndex = D3D12CalcSubresource(mipLevel, faceIndex, 0, numMipLevels, 6);

                    DirectX::Image image;
                    image.width = resourceDesc.Width >> mipLevel;
                    image.height = resourceDesc.Height >> mipLevel;
                    image.format = resourceDesc.Format;
                    image.rowPitch = static_cast<size_t>(layouts[subresourceIndex].Footprint.RowPitch);
                    image.slicePitch = image.rowPitch * image.height;
                    image.pixels = static_cast<uint8_t*>(mappedData) + layouts[subresourceIndex].Offset;

                    const DirectX::Image* destImage = scratchImage.GetImage(mipLevel, faceIndex, 0);

                    // Sometimes, due to formatting weirdness, the row pitch of the source image is less than the row pitch of the destination image
                    // We copy row by row to avoid heap corruption
                    // I don't know why ScratchImage does this
                    // TODO: Does this break smaller mip levels?

                    size_t destRowPitch = destImage->rowPitch;
                    size_t srcRowPitch = image.rowPitch;
                    size_t rowCount = image.height;

                    uint8_t* destPixels = destImage->pixels;
                    uint8_t* srcPixels = image.pixels;

                    for (size_t row = 0; row < rowCount; ++row)
                    {
                        memcpy(destPixels + row * destRowPitch, srcPixels + row * srcRowPitch, (std::min)(destRowPitch, srcRowPitch));
                    }

                    //memcpy(destImage->pixels, image.pixels, image.slicePitch);
                }
            }

            readbackBuffer->Unmap(0, nullptr);

            hr = DirectX::SaveToDDSFile(scratchImage.GetImages(), scratchImage.GetImageCount(), scratchImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, outputFile.c_str());
            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to save the cubemap to a .dds file!");
            }
            // Save asynchronously to a DDS file with mipmaps
            //std::async(std::launch::async, AsyncSaveToDDS, std::move(scratchImage), outputFile);
            }).detach();
        };
    std::lock_guard<std::mutex> lock(readbackRequestsMutex);
    m_readbackRequests.push_back(std::move(readbackRequest));
}

void ReadbackManager::SaveTextureToDDS(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12CommandQueue* commandQueue, Texture* texture, const std::wstring& outputFile, UINT64 fenceValue) {
    D3D12_RESOURCE_DESC resourceDesc = texture->GetBuffer()->GetTexture()->GetDesc();

    // Get the number of mip levels and subresources
    UINT numMipLevels = resourceDesc.MipLevels;
    UINT numSubresources = numMipLevels;

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(numSubresources);
    std::vector<UINT> numRows(numSubresources);
    std::vector<UINT64> rowSizesInBytes(numSubresources);
    UINT64 totalSize = 0;

    // Get the copyable footprints for each mip level of each face
    device->GetCopyableFootprints(
        &resourceDesc,
        0,
        numSubresources,
        0,
        layouts.data(),
        numRows.data(),
        rowSizesInBytes.data(),
        &totalSize);

    // Describe and create the readback buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = totalSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;  // Buffers don't have a format
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;  // Buffers should be row-major
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> readbackBuffer;
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer));
    if (FAILED(hr))
    {
        throw std::runtime_error("Failed to create readback buffer!");
    }
    readbackBuffer->SetName(L"Readback");

    auto initialState = ResourceStateToD3D12(texture->GetState());
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture->GetBuffer()->GetTexture().Get(), initialState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->ResourceBarrier(1, &barrier);

    // Issue copy commands for each mip level of each face
    for (UINT mipLevel = 0; mipLevel < numMipLevels; ++mipLevel) {
        UINT subresourceIndex = D3D12CalcSubresource(mipLevel, 0, 0, numMipLevels, 1);

        D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
        srcLocation.pResource = texture->GetBuffer()->GetTexture().Get();
        srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLocation.SubresourceIndex = subresourceIndex;

        D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
        dstLocation.pResource = readbackBuffer.Get();
        dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLocation.PlacedFootprint = layouts[subresourceIndex];

        commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

    }

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture->GetBuffer()->GetTexture().Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, initialState);
    commandList->ResourceBarrier(1, &barrier);

    ReadbackRequest readbackRequest;
    readbackRequest.readbackBuffer = readbackBuffer;
    readbackRequest.layouts = layouts;
    readbackRequest.totalSize = totalSize;
    readbackRequest.outputFile = outputFile;
    readbackRequest.fenceValue = fenceValue;
    readbackRequest.callback = [=]() {
        std::thread([=] {
            void* mappedData = nullptr;
            D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(totalSize) };
            auto hr = readbackBuffer->Map(0, &readRange, &mappedData);
            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to map the readback buffer!");
            }

            // Create a ScratchImage and fill it with the cubemap and mipmap data
            DirectX::ScratchImage scratchImage;
            scratchImage.Initialize2D(resourceDesc.Format, resourceDesc.Width, resourceDesc.Height, 1, numMipLevels);

            for (UINT mipLevel = 0; mipLevel < numMipLevels; ++mipLevel) {
                UINT subresourceIndex = D3D12CalcSubresource(mipLevel, 0, 0, numMipLevels, 6);

                DirectX::Image image;
                image.width = resourceDesc.Width >> mipLevel;
                image.height = resourceDesc.Height >> mipLevel;
                image.format = resourceDesc.Format;
                image.rowPitch = static_cast<size_t>(layouts[subresourceIndex].Footprint.RowPitch);
                image.slicePitch = image.rowPitch * image.height;
                image.pixels = static_cast<uint8_t*>(mappedData) + layouts[subresourceIndex].Offset;

                const DirectX::Image* destImage = scratchImage.GetImage(mipLevel, 0, 0);

                // Sometimes, due to formatting weirdness, the row pitch of the source image is less than the row pitch of the destination image
                // We copy row by row to avoid heap corruption
                // I don't know why ScratchImage does this
                // TODO: Does this break smaller mip levels?

                size_t destRowPitch = destImage->rowPitch;
                size_t srcRowPitch = image.rowPitch;
                size_t rowCount = image.height;

                uint8_t* destPixels = destImage->pixels;
                uint8_t* srcPixels = image.pixels;

                for (size_t row = 0; row < rowCount; ++row)
                {
                    memcpy(destPixels + row * destRowPitch, srcPixels + row * srcRowPitch, (std::min)(destRowPitch, srcRowPitch));
                }

                //memcpy(destImage->pixels, image.pixels, image.slicePitch);

            }

            readbackBuffer->Unmap(0, nullptr);

            hr = DirectX::SaveToDDSFile(scratchImage.GetImages(), scratchImage.GetImageCount(), scratchImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, outputFile.c_str());
            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to save the cubemap to a .dds file!");
            }
            // Save asynchronously to a DDS file with mipmaps
            //std::async(std::launch::async, AsyncSaveToDDS, std::move(scratchImage), outputFile);
            }).detach();
        };

    std::lock_guard<std::mutex> lock(readbackRequestsMutex);
    m_readbackRequests.push_back(std::move(readbackRequest));
}

void ReadbackManager::ProcessReadbackRequests() {
    std::lock_guard<std::mutex> lock(readbackRequestsMutex);

    std::vector<ReadbackRequest> remainingRequests;
    for (auto& request : m_readbackRequests) {
        // Check if the GPU has completed the work for this readback
        if (m_readbackFence->GetCompletedValue() >= request.fenceValue) {
            request.callback();
        }
        else {
            // Keep the request in the queue for the next frame
            remainingRequests.push_back(std::move(request));
        }
    }

    // Update the queue with remaining requests
    m_readbackRequests = std::move(remainingRequests);
}