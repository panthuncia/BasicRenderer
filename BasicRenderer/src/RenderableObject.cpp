#include "RenderableObject.h"

#include "DirectX/d3dx12.h"
#include "Buffers.h"
#include "Utilities.h"
#include "DeviceManager.h"
#include "Material.h"
RenderableObject::RenderableObject(std::wstring name) : SceneNode(name) {
    CreateBuffers();
}

RenderableObject::RenderableObject(std::wstring name, std::vector<Mesh> meshes) : SceneNode(name) {
    for (auto& mesh : meshes) {
        if (mesh.material->m_blendState != BlendState::BLEND_STATE_OPAQUE) {
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

RenderableObject::RenderableObject(std::wstring name, std::vector<Mesh>& newOpaqueMeshes, std::vector<Mesh>& newTransparentMeshes) : SceneNode(name) {
    if (newOpaqueMeshes.size() > 0) {
        m_hasOpaque = true;
        for (auto& mesh : newOpaqueMeshes) {
            opaqueMeshes.push_back(mesh);
        }
    }
    if (newTransparentMeshes.size() > 0) {
        m_hasTransparent = true;
        for (auto& mesh : newTransparentMeshes) {
            transparentMeshes.push_back(mesh);
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
	auto& resourceManager = ResourceManager::GetInstance();
    perObjectConstantBuffer = resourceManager.CreateConstantBuffer<PerObjectCB>();
	resourceManager.UpdateConstantBuffer(perObjectConstantBuffer, perObjectCBData);
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

    auto& resourceManager = ResourceManager::GetInstance();
    resourceManager.UpdateConstantBuffer(perObjectConstantBuffer, perObjectCBData);
}

BufferHandle& RenderableObject::GetConstantBuffer() {
    return perObjectConstantBuffer;
}

void RenderableObject::OnUpdate() {
    UpdateBuffers();
}

void RenderableObject::SetSkin(std::shared_ptr<Skeleton> skeleton) {
    m_skeleton = skeleton;
    perObjectCBData.boneTransformBufferIndex = skeleton->GetTransformsBufferIndex();
    perObjectCBData.inverseBindMatricesBufferIndex = skeleton->GetInverseBindMatricesBufferIndex();
    UpdateBuffers();
    skeleton->userIDs.push_back(localID);
}