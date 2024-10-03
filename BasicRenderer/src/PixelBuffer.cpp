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
    handle = resourceManager.CreateTextureFromImage(image, width, height, channels, sRGB, m_format);
    SetIndex(GetSRVDescriptorIndex());
    currentState = ResourceState::UNKNOWN;
    m_width = width;
    m_height = height;
	m_channels = channels;
}

PixelBuffer::PixelBuffer(const std::array<const stbi_uc*, 6>& images, int width, int height, int channels, bool sRGB) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();
    m_format = DetermineTextureFormat(channels, sRGB, false);
    handle = resourceManager.CreateCubemapFromImages(images, width, height, channels, sRGB, m_format);
    SetIndex(GetSRVDescriptorIndex());
    currentState = ResourceState::UNKNOWN;
	m_width = width;
	m_height = height;
	m_channels = channels;
}

PixelBuffer::PixelBuffer(int width, int height, int channels, bool isCubemap, bool RTV, bool DSV, bool UAV) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();
    m_format = DetermineTextureFormat(channels, false, DSV);
    handle = resourceManager.CreateTexture(width, height, channels, m_format, isCubemap, RTV, DSV, UAV);
    SetIndex(GetSRVDescriptorIndex());
    currentState = ResourceState::UNKNOWN;
	m_width = width;
	m_height = height;
	m_channels = channels;
}

PixelBuffer::PixelBuffer(int width, int height, int channels, int numTextures, bool isCubemap, bool RTV, bool DSV, bool UAV) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();
    m_format = DetermineTextureFormat(channels, false, DSV);
    handle = resourceManager.CreateTextureArray(width, height, channels, numTextures, isCubemap, m_format, RTV, DSV, UAV);
    SetIndex(GetSRVDescriptorIndex());
    currentState = ResourceState::UNKNOWN;
	m_width = width;
	m_height = height;
	m_channels = channels;
}

UINT PixelBuffer::GetSRVDescriptorIndex() const {
    return handle.SRVInfo.index;
}

void PixelBuffer::Transition(const RenderContext& context, ResourceState fromState, ResourceState toState) {
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

    context.commandList->ResourceBarrier(1, &barrier);

    currentState = toState;
}