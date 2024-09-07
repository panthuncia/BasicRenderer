#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <stdint.h>
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

class Buffer {
public:
	Buffer(ID3D12Device* device, ResourceCPUAccessType accessType, uint32_t bufferSize, ResourceUsageType usageType);
	~Buffer() = default;
	ResourceCPUAccessType m_accessType;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_buffer;
};