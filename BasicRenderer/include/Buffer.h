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

class Buffer {
public:
	Buffer(ID3D12Device* device, ResourceCPUAccessType accessType, uint32_t bufferSize);
	~Buffer() = default;
	ResourceCPUAccessType m_accessType;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_buffer;
};