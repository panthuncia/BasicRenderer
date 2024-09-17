#include "Texture.h"
#include "Sampler.h"
#include "PixelBuffer.h"
#include "RenderContext.h"

Texture::Texture(std::shared_ptr<PixelBuffer> image, std::shared_ptr<Sampler> sampler) {
	m_image = image;
	m_sampler = sampler;
}

UINT Texture::GetBufferDescriptorIndex() {
	return m_image->GetSRVDescriptorIndex();
}

UINT Texture::GetSamplerDescriptorIndex() {
	return m_sampler->GetDescriptorIndex();
}

TextureHandle<PixelBuffer>& Texture::GetHandle() {
	return m_image->handle;
}

void Texture::Transition(RenderContext& context, ResourceState fromState, ResourceState toState) {
    if (fromState == toState) return; // No transition needed

    D3D12_RESOURCE_STATES d3dFromState = ResourceStateToD3D12(fromState);
    D3D12_RESOURCE_STATES d3dToState = ResourceStateToD3D12(toState);

    // Create a resource barrier
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_image->handle.texture.Get();
    barrier.Transition.StateBefore = d3dFromState;
    barrier.Transition.StateAfter = d3dToState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    // Record the barrier into the command list
    context.commandList->ResourceBarrier(1, &barrier);

    // Update the current state
    currentState = toState;
}