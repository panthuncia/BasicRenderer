#include "Managers/Singletons/ReadbackManager.h"

#include <DirectXTex.h>
#include <rhi.h>
#include <rhi_helpers.h>
#include <rhi_conversions_dx12.h>

#include "Resources/Texture.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Buffers/Buffer.h"

std::unique_ptr<ReadbackManager> ReadbackManager::instance = nullptr;
bool ReadbackManager::initialized = false;

void ReadbackManager::SaveCubemapToDDS(rhi::Device& device, rhi::CommandList& commandList, PixelBuffer* cubemap, const std::wstring& outputFile, UINT64 fenceValue) {
    // Get the number of mip levels and subresources
    uint32_t numMipLevels = cubemap->GetMipLevels();
    const uint32_t faces = 6;
    uint32_t numSubresources = faces * numMipLevels;  // 6 faces, each with multiple mip levels

    std::vector<rhi::CopyableFootprint> fps;
    fps.resize(numMipLevels * faces);

    rhi::FootprintRangeDesc fr{};
    fr.texture = cubemap->GetAPIResource().GetHandle();
    fr.firstMip = 0;
    fr.mipCount = numMipLevels;
    fr.firstArraySlice = 0;
    fr.arraySize = faces;
    fr.firstPlane = 0;
    fr.planeCount = 1;
    fr.baseOffset = 0;      // start of the staging buffer

    auto info = device.GetCopyableFootprints(fr, fps.data(), (uint32_t)fps.size());
    assert(info.count == numMipLevels * faces);


	auto readbackBuffer = Buffer::CreateShared(rhi::HeapType::Readback, info.totalBytes);
    readbackBuffer->SetName(L"Readback");

    //auto initialState = ResourceStateToD3D12(cubemap->GetState());
    //CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(cubemap->GetTexture().Get(), initialState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    //commandList->ResourceBarrier(1, &barrier);

    // Issue copy commands for each mip level of each face
    for (UINT mipLevel = 0; mipLevel < numMipLevels; ++mipLevel) {
        for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex) {
            UINT subresourceIndex = CalcSubresource(mipLevel, faceIndex, 0, numMipLevels, 6);

            rhi::BufferTextureCopyFootprint c{};
            c.texture = cubemap->GetAPIResource().GetHandle();
            c.buffer = readbackBuffer->GetAPIResource().GetHandle();
            c.mip = mipLevel;
            c.arraySlice = faceIndex;
            c.x = c.y = c.z = 0;     // full subresource
            c.footprint.offset = fps[subresourceIndex].offset;
            c.footprint.rowPitch = fps[subresourceIndex].rowPitch;
            c.footprint.width = fps[subresourceIndex].width;
            c.footprint.height = fps[subresourceIndex].height;
            c.footprint.depth = fps[subresourceIndex].depth;

            commandList.CopyTextureToBuffer(c);
        }
    }

    //barrier = CD3DX12_RESOURCE_BARRIER::Transition(cubemap->GetTexture().Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, initialState);
    //commandList->ResourceBarrier(1, &barrier);

    auto width = cubemap->GetWidth();
    auto height = cubemap->GetHeight();
    auto format = rhi::ToDxgi(cubemap->GetFormat());
    ReadbackRequest readbackRequest;
    readbackRequest.readbackBuffer = readbackBuffer->GetAPIResource().GetHandle();
    readbackRequest.layouts = fps;
    readbackRequest.totalSize = info.totalBytes;
    readbackRequest.outputFile = outputFile;
    readbackRequest.fenceValue = fenceValue;
    readbackRequest.callback = [=]() {
        std::thread([=] {
            void* mappedData = nullptr;
            readbackBuffer->GetAPIResource().Map(&mappedData);

            // Create a ScratchImage and fill it with the cubemap and mipmap data
            DirectX::ScratchImage scratchImage;
            scratchImage.InitializeCube(format, width, height, 1, numMipLevels);  // Initialize with mip levels

            for (UINT mipLevel = 0; mipLevel < numMipLevels; ++mipLevel) {
                for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex) {
                    UINT subresourceIndex = CalcSubresource(mipLevel, faceIndex, 0, numMipLevels, 6);

                    DirectX::Image image;
                    image.width = fps[subresourceIndex].width;
                    image.height = fps[subresourceIndex].height;
                    image.format = format;
                    image.rowPitch = static_cast<size_t>(fps[subresourceIndex].rowPitch);
                    image.slicePitch = image.rowPitch * image.height;
                    image.pixels = static_cast<uint8_t*>(mappedData) + fps[subresourceIndex].offset;

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

            readbackBuffer->GetAPIResource().Unmap(0, 0);

            auto hr = DirectX::SaveToDDSFile(scratchImage.GetImages(), scratchImage.GetImageCount(), scratchImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, outputFile.c_str());
            if (FAILED(hr))
            {
                spdlog::error("Failed to save the cubemap to a .dds file!");
            }
            // Save asynchronously to a DDS file with mipmaps
            //std::async(std::launch::async, AsyncSaveToDDS, std::move(scratchImage), outputFile);
            }).detach();
        };
    std::lock_guard<std::mutex> lock(readbackRequestsMutex);
    m_readbackRequests.push_back(std::move(readbackRequest));
}

void ReadbackManager::SaveTextureToDDS(
    rhi::Device& device,
    rhi::CommandList& commandList,
    PixelBuffer* texture,
    const std::wstring& outputFile,
    uint64_t fenceValue)
{
    // Mips / subresources (single 2D texture, no array)
    const uint32_t numMipLevels = texture->GetMipLevels();
    const uint32_t faces = 1;
    const uint32_t numSubresources = numMipLevels * faces;

    // Ask RHI for copyable footprints
    std::vector<rhi::CopyableFootprint> fps(numSubresources);

    rhi::FootprintRangeDesc fr{};
    fr.texture = texture->GetAPIResource().GetHandle();
    fr.firstMip = 0;
    fr.mipCount = numMipLevels;
    fr.firstArraySlice = 0;
    fr.arraySize = faces;     // 1 for a single 2D texture
    fr.firstPlane = 0;
    fr.planeCount = 1;
    fr.baseOffset = 0;

    auto info = device.GetCopyableFootprints(fr, fps.data(), (uint32_t)fps.size());
    assert(info.count == numSubresources);

    // Create a readback buffer sized to hold all subresources
    auto readbackBuffer = Buffer::CreateShared(rhi::HeapType::Readback, info.totalBytes);
    readbackBuffer->SetName(L"Readback");

    // Issue copy commands for each mip
    for (uint32_t mipLevel = 0; mipLevel < numMipLevels; ++mipLevel)
    {
        const uint32_t subresourceIndex = CalcSubresource(mipLevel, 0, 0, numMipLevels, /*arraySize*/1);

        rhi::BufferTextureCopyFootprint c{};
        c.texture = texture->GetAPIResource().GetHandle();
        c.buffer = readbackBuffer->GetAPIResource().GetHandle();
        c.mip = mipLevel;
        c.arraySlice = 0;
        c.x = c.y = c.z = 0; // full subresource

        // Pass the exact placed footprint returned by the device
        c.footprint.offset = fps[subresourceIndex].offset;
        c.footprint.rowPitch = fps[subresourceIndex].rowPitch;
        c.footprint.width = fps[subresourceIndex].width;
        c.footprint.height = fps[subresourceIndex].height;
        c.footprint.depth = fps[subresourceIndex].depth;

        commandList.CopyTextureToBuffer(c);
    }

    // Defer readback + DDS write (same pattern as your cubemap path)
    ReadbackRequest readbackRequest;
    readbackRequest.readbackBuffer = readbackBuffer->GetAPIResource().GetHandle();
    readbackRequest.layouts = fps;
    readbackRequest.totalSize = info.totalBytes;
    readbackRequest.outputFile = outputFile;
    readbackRequest.fenceValue = fenceValue;
    readbackRequest.callback = [=]() {
        std::thread([=] {
            void* mappedData = nullptr;
            readbackBuffer->GetAPIResource().Map(&mappedData);

            DirectX::ScratchImage scratchImage;
            const auto dxgiFmt = rhi::ToDxgi(texture->GetFormat());
            scratchImage.Initialize2D(
                dxgiFmt,
                texture->GetWidth(),
                texture->GetHeight(),
                /*arraySize*/1,
                /*mipLevels*/numMipLevels);

            for (uint32_t mipLevel = 0; mipLevel < numMipLevels; ++mipLevel)
            {
                const uint32_t subresourceIndex = CalcSubresource(
                    mipLevel, /*arraySlice*/0, /*plane*/0, numMipLevels, /*arraySize*/1);

                DirectX::Image src{};
                src.width = fps[subresourceIndex].width;
                src.height = fps[subresourceIndex].height;
                src.format = dxgiFmt;
                src.rowPitch = static_cast<size_t>(fps[subresourceIndex].rowPitch);
                src.slicePitch = src.rowPitch * src.height;
                src.pixels = static_cast<uint8_t*>(mappedData) + fps[subresourceIndex].offset;

                const DirectX::Image* dst = scratchImage.GetImage(mipLevel, /*item*/0, /*slice*/0);

                // Defensive row-by-row copy (rowPitch mismatches can happen)
                const size_t dstRowPitch = dst->rowPitch;
                const size_t srcRowPitch = src.rowPitch;
                const size_t rows = src.height;

                uint8_t* dstPixels = dst->pixels;
                const uint8_t* srcPixels = src.pixels;

                for (size_t row = 0; row < rows; ++row)
                {
                    memcpy(dstPixels + row * dstRowPitch,
                        srcPixels + row * srcRowPitch,
                        (std::min)(dstRowPitch, srcRowPitch));
                }
            }

            readbackBuffer->GetAPIResource().Unmap(0, 0);

            auto hr = DirectX::SaveToDDSFile(
                scratchImage.GetImages(),
                scratchImage.GetImageCount(),
                scratchImage.GetMetadata(),
                DirectX::DDS_FLAGS_NONE,
                outputFile.c_str());

            if (FAILED(hr)) {
                spdlog::error("Failed to save the texture to a .dds file!");
            }
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
        if (m_readbackFence.GetCompletedValue() >= request.fenceValue) {
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