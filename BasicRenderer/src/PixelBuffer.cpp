#include "PixelBuffer.h"
#include <string>
#include <stdexcept>

#include "DeviceManager.h"
#include "Utilities.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "ResourceManager.h"
#include "RenderContext.h"

PixelBuffer::PixelBuffer(const stbi_uc* image, int width, int height, int channels, bool sRGB) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();
    m_format = DetermineTextureFormat(channels, sRGB, false);

	TextureDescription desc;
	desc.width = width;
	desc.height = height;
	desc.channels = channels;
	desc.isCubemap = false;
	desc.hasRTV = false;
	desc.hasDSV = false;
	desc.hasUAV = false;
	desc.generateMipMaps = false;
	desc.arraySize = 1;
	desc.isCubemap = false;
	desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.isArray = false;

	handle = resourceManager.CreateTexture(desc, {image});
	//handle = resourceManager.CreateTextureFromImage(image, width, height, channels, sRGB, m_format);

    SetIndex(GetSRVDescriptorIndex());
    currentState = ResourceState::UNKNOWN;
    m_width = width;
    m_height = height;
	m_channels = channels;
}

PixelBuffer::PixelBuffer(const std::array<const stbi_uc*, 6>& images, int width, int height, int channels, bool sRGB) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();
    m_format = DetermineTextureFormat(channels, sRGB, false);

    TextureDescription desc;
	desc.width = width;
	desc.height = height;
	desc.channels = channels;
	desc.isCubemap = true;
	desc.hasRTV = false;
	desc.hasDSV = false;
	desc.hasUAV = false;
	desc.generateMipMaps = false;
	desc.arraySize = 1;
	desc.isCubemap = true;
	desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.isArray = false;

    handle = resourceManager.CreateTexture(desc, images);

    SetIndex(GetSRVDescriptorIndex());
    currentState = ResourceState::UNKNOWN;
	m_width = width;
	m_height = height;
	m_channels = channels;
}

PixelBuffer::PixelBuffer(const TextureDescription& desc) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();

    handle = resourceManager.CreateTexture(desc);
    SetIndex(GetSRVDescriptorIndex());
    currentState = ResourceState::UNKNOWN;
	m_width = desc.width;
	m_height = desc.height;
	m_channels = desc.channels;
}

UINT PixelBuffer::GetSRVDescriptorIndex() const {
    return handle.SRVInfo.index;
}

void PixelBuffer::Transition(ID3D12GraphicsCommandList* commandList, ResourceState fromState, ResourceState toState) {
    if (fromState == toState) return;

    D3D12_RESOURCE_STATES d3dFromState = ResourceStateToD3D12(fromState);
    D3D12_RESOURCE_STATES d3dToState = ResourceStateToD3D12(toState);

    // Create a resource barrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = GetHandle().texture.Get();
    barrier.Transition.StateBefore = d3dFromState;
    barrier.Transition.StateAfter = d3dToState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(1, &barrier);

    currentState = toState;
}