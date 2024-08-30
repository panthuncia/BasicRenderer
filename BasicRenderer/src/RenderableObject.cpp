#include "RenderableObject.h"

#include "DirectX/d3dx12.h"
#include "Buffers.h"
#include "Utilities.h"
#include "DeviceManager.h"
RenderableObject::RenderableObject(std::string name) : SceneNode(name) {
    CreateBuffers();
}

RenderableObject::RenderableObject(std::string name, std::vector<Mesh> meshes) : SceneNode(name) {
    for (auto& mesh : meshes) {
        if (mesh.material->blendState != BlendState::BLEND_STATE_OPAQUE) {
            transparentMeshes.push_back(mesh);
            m_hasTransparent = true;
        }
        else {
            this->opaqueMeshes.push_back(mesh);
            m_hasOpaque = true;
        }
    }
    CreateBuffers();
}

std::vector<Mesh>& RenderableObject::GetOpaqueMeshes() {
	return opaqueMeshes;
}

std::vector<Mesh>& RenderableObject::GetTransparentMeshes() {
    return transparentMeshes;
}

bool RenderableObject::HasOpaque() const {
    return m_hasOpaque;
}

bool RenderableObject::HasTransparent() const {
    return m_hasTransparent;
}

void RenderableObject::CreateBuffers() {
    // Create PerMesh buffer
    perObjectConstantBuffer = CreateConstantBuffer<PerObjectCB>(&perObjectCBData);
}

void RenderableObject::UpdateBuffers() {
    perObjectCBData.modelMatrix = transform.modelMatrix;

    XMMATRIX upperLeft3x3 = XMMatrixSet(
        XMVectorGetX(perObjectCBData.modelMatrix.r[0]), XMVectorGetY(perObjectCBData.modelMatrix.r[0]), XMVectorGetZ(perObjectCBData.modelMatrix.r[0]), 0.0f,
        XMVectorGetX(perObjectCBData.modelMatrix.r[1]), XMVectorGetY(perObjectCBData.modelMatrix.r[1]), XMVectorGetZ(perObjectCBData.modelMatrix.r[1]), 0.0f,
        XMVectorGetX(perObjectCBData.modelMatrix.r[2]), XMVectorGetY(perObjectCBData.modelMatrix.r[2]), XMVectorGetZ(perObjectCBData.modelMatrix.r[2]), 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    perObjectCBData.normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, upperLeft3x3));

    D3D12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(perObjectConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pPerObjectConstantBuffer)));
    memcpy(pPerObjectConstantBuffer, &perObjectCBData, sizeof(perObjectCBData));
    perObjectConstantBuffer->Unmap(0, nullptr);
}

ComPtr<ID3D12Resource>& RenderableObject::GetConstantBuffer() {
    return perObjectConstantBuffer;
}

void RenderableObject::onUpdate() {
    UpdateBuffers();
}

void RenderableObject::SetSkin(std::shared_ptr<Skeleton> skeleton) {
    m_skeleton = skeleton;
    skeleton->userIDs.push_back(localID);
}