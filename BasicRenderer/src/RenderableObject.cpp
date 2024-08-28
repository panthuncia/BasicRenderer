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
    perObjectConstantBuffer = CreateConstantBuffer<PerObjectCB>(&perObjectCBData);
}

void RenderableObject::UpdateBuffers() {
    D3D12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(perObjectConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pPerObjectConstantBuffer)));
    perObjectCBData.model = transform.modelMatrix;
    memcpy(pPerObjectConstantBuffer, &perObjectCBData, sizeof(perObjectCBData));
    perObjectConstantBuffer->Unmap(0, nullptr);
}

ComPtr<ID3D12Resource>& RenderableObject::getConstantBuffer() {
    return perObjectConstantBuffer;
}

void RenderableObject::onUpdate() {
    UpdateBuffers();
}