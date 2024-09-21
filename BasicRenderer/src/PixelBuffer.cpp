#include "PixelBuffer.h"
#include <string>
#include <stdexcept>

#include "DeviceManager.h"
#include "Utilities.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "ResourceManager.h"

PixelBuffer::PixelBuffer(const stbi_uc* image, int width, int height, int channels, bool sRGB) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();
    handle = resourceManager.CreateTextureFromImage(image, width, height, channels, sRGB);
    SetIndex(GetSRVDescriptorIndex());
    currentState = ResourceState::Common;
}

PixelBuffer::PixelBuffer(int width, int height, int channels, bool isCubemap, bool RTV, bool DSV, bool UAV) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();
    handle = resourceManager.CreateTexture(width, height, channels, isCubemap, RTV, DSV, UAV);
    SetIndex(GetSRVDescriptorIndex());
    currentState = ResourceState::Common;
}

PixelBuffer::PixelBuffer(int width, int height, int channels, int numTextures, bool isCubemap, bool RTV, bool DSV, bool UAV) {
    ResourceManager& resourceManager = ResourceManager::GetInstance();
    handle = resourceManager.CreateTextureArray(width, height, channels, numTextures, isCubemap, RTV, DSV, UAV);
    SetIndex(GetSRVDescriptorIndex());
    currentState = ResourceState::Common;
}

UINT PixelBuffer::GetSRVDescriptorIndex() const {
    return handle.SRVInfo.index;
}

void PixelBuffer::Transition(RenderContext& context, ResourceState fromState, ResourceState toState) {
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