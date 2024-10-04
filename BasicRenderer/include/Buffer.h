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

D3D12_HEAP_TYPE TranslateAccessType(ResourceCPUAccessType accessType);

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
	Microsoft::WRL::ComPtr<ID3D12Resource> m_buffer;
	void Transition(ID3D12GraphicsCommandList* commandList, ResourceState prevState, ResourceState newState);
protected:
	void OnSetName() override { m_buffer->SetName(name.c_str()); }
private:
	Buffer(ID3D12Device* device, ResourceCPUAccessType accessType, uint32_t bufferSize, bool upload);
};