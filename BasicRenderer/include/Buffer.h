#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <stdint.h>
#include <string>
#include <memory>
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
	ALL_SRV,
	RENDER_TARGET,
	DEPTH_STENCIL,
	UPLOAD
};

struct ResourceTransition {
	ID3D12Resource* resource;
	D3D12_RESOURCE_STATES beforeState;
	D3D12_RESOURCE_STATES afterState;
};

D3D12_HEAP_TYPE TranslateAccessType(ResourceCPUAccessType accessType);
D3D12_RESOURCE_STATES TranslateUsageType(ResourceUsageType usageType);

class RenderContext;

class Buffer : public Resource{
public:
	static std::shared_ptr<Buffer> CreateShared(ID3D12Device* device, ResourceCPUAccessType accessType, uint32_t bufferSize, bool upload) {
		return std::shared_ptr<Buffer>(new Buffer(device, accessType, bufferSize, upload));
	}
	static std::unique_ptr<Buffer> CreateUnique(ID3D12Device* device, ResourceCPUAccessType accessType, uint32_t bufferSize, bool upload) {
		return std::unique_ptr<Buffer>(new Buffer(device, accessType, bufferSize, upload));
	}
	~Buffer() = default;
	ResourceCPUAccessType m_accessType;
	ResourceUsageType m_usageType;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_buffer;
	void SetName(const std::wstring& name) { m_buffer->SetName(name.c_str()); }
	void Transition(RenderContext& context, ResourceState prevState, ResourceState newState);
private:
	Buffer(ID3D12Device* device, ResourceCPUAccessType accessType, uint32_t bufferSize, bool upload);
};