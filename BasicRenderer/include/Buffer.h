#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <stdint.h>
#include <string>
#include "Resource.h"
enum class ResourceCPUAccessType {
	READ,
	WRITE,
	READ_WRITE,
	NONE
};

enum class ResourceUsageType {
	UNKNOWN,
	INDEX,
	VERTEX,
	CONSTANT,
	PIXEL_SRV,
	NON_PIXEL_SRV,
	RENDER_TARGET,
	DEPTH_STENCIL,
	UPLOAD
};

struct ResourceTransition {
	ID3D12Resource* resource;
	D3D12_RESOURCE_STATES beforeState;
	D3D12_RESOURCE_STATES afterState;
};

class RenderContext;

class Buffer : public Resource{
public:
	Buffer(ID3D12Device* device, ResourceCPUAccessType accessType, uint32_t bufferSize, ResourceUsageType usageType);
	~Buffer() = default;
	ResourceCPUAccessType m_accessType;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_buffer;
	void SetName(const std::wstring& name) { m_buffer->SetName(name.c_str()); }
	void Transition(RenderContext& context, ResourceState prevState, ResourceState newState);
};