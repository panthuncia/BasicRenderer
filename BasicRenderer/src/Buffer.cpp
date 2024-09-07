#include "Buffer.h"

#include <stdexcept>

#include "DirectX/d3dx12.h"
#include "spdlog/spdlog.h"

using namespace Microsoft::WRL;

D3D12_HEAP_TYPE translateAccessType(ResourceCPUAccessType accessType) {
	switch (accessType) {
	case ResourceCPUAccessType::READ:
		return D3D12_HEAP_TYPE_READBACK;
	case ResourceCPUAccessType::WRITE:
		return D3D12_HEAP_TYPE_UPLOAD;
	case ResourceCPUAccessType::READ_WRITE:
		return D3D12_HEAP_TYPE_UPLOAD;
	case ResourceCPUAccessType::NONE:
		return D3D12_HEAP_TYPE_DEFAULT;
	}
}

D3D12_RESOURCE_STATES translateUsageType(ResourceUsageType usageType) {
	switch (usageType) {
	case ResourceUsageType::UNKNOWN:
		return D3D12_RESOURCE_STATE_COMMON;
	case ResourceUsageType::UPLOAD:
		return D3D12_RESOURCE_STATE_GENERIC_READ;
	}
	return D3D12_RESOURCE_STATE_COMMON;
}

Buffer::Buffer(ID3D12Device* device, ResourceCPUAccessType accessType, uint32_t bufferSize, ResourceUsageType usageType = ResourceUsageType::UNKNOWN) {
	m_accessType = accessType;
	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(translateAccessType(accessType));
	D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
	auto hr = device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		translateUsageType(usageType),
		nullptr,
		IID_PPV_ARGS(&m_buffer));

	if (FAILED(hr)) {
		spdlog::error("HRESULT failed with error code: {}", hr);
		throw std::runtime_error("HRESULT failed");
	}
}