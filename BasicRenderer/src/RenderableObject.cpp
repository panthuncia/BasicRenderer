#include "RenderableObject.h"

#include "DirectX/d3dx12.h"
#include "Buffers.h"
#include "Utilities.h"
#include "DeviceManager.h"
RenderableObject::RenderableObject(std::string name) : SceneNode(name) {
    CreateBuffers();
}

RenderableObject::RenderableObject(std::string name, std::vector<Mesh> meshes) : SceneNode(name) {
	this->meshes = meshes;
    CreateBuffers();
}

std::vector<Mesh>& RenderableObject::getMeshes() {
	return meshes;
}

void RenderableObject::CreateBuffers() {
    // Create PerMesh buffer
    UINT constantBufferSize = sizeof(PerObjectCB);

    // Describe and create a constant buffer
    CD3DX12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);

    auto device = DeviceManager::getInstance().getDevice();

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&perMeshConstantBuffer)));

    // Map the constant buffer and initialize it
    D3D12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(perMeshConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pPerMeshConstantBuffer)));

    // Initialize the constant buffer data
    memcpy(pPerMeshConstantBuffer, &perMeshCBData, sizeof(perMeshCBData));
    perMeshConstantBuffer->Unmap(0, nullptr);
}

void RenderableObject::UpdateBuffers() {
    perMeshCBData.model = transform.modelMatrix;//DirectX::XMMatrixMultiply(DirectX::XMMatrixRotationY(DirectX::XM_PIDIV4), DirectX::XMMatrixTranslation(0, 1, -2));
    memcpy(pPerMeshConstantBuffer, &perMeshCBData, sizeof(perMeshCBData));
}

ComPtr<ID3D12Resource>& RenderableObject::getConstantBuffer() {
    return perMeshConstantBuffer;
}

void RenderableObject::onUpdate() {
    UpdateBuffers();
}