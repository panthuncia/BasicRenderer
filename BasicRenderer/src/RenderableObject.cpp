#include "RenderableObject.h"

#include "DirectX/d3dx12.h"
#include "Buffers.h"
#include "Utilities.h"
#include "DeviceManager.h"
#include "Material.h"
#include "ObjectManager.h"
RenderableObject::RenderableObject(std::wstring name) : SceneNode(name) {
}

RenderableObject::RenderableObject(std::wstring name, std::vector<std::shared_ptr<Mesh>> meshes) : SceneNode(name) {
    for (auto& mesh : meshes) {
        if (mesh->material->m_blendState != BlendState::BLEND_STATE_OPAQUE) {
            transparentMeshes.push_back(mesh);
            m_hasTransparent = true;
        }
        else {
            this->opaqueMeshes.push_back(mesh);
            m_hasOpaque = true;
        }
    }
}

RenderableObject::RenderableObject(std::wstring name, std::vector<std::shared_ptr<Mesh>>& newOpaqueMeshes, std::vector<std::shared_ptr<Mesh>>& newTransparentMeshes) : SceneNode(name) {
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
}

std::vector<std::shared_ptr<Mesh>>& RenderableObject::GetOpaqueMeshes() {
	return opaqueMeshes;
}

std::vector<std::shared_ptr<Mesh>>& RenderableObject::GetTransparentMeshes() {
    return transparentMeshes;
}

bool RenderableObject::HasOpaque() const {
    return m_hasOpaque;
}

bool RenderableObject::HasTransparent() const {
    return m_hasTransparent;
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
    m_currentManager->UpdatePerObjectBuffer(m_perObjectCBView, perObjectCBData);
}

void RenderableObject::OnUpdate() {
    UpdateBuffers();
}

void RenderableObject::SetSkin(std::shared_ptr<Skeleton> skeleton) {
    m_skeleton = skeleton;
    perObjectCBData.boneTransformBufferIndex = skeleton->GetTransformsBufferIndex();
    perObjectCBData.inverseBindMatricesBufferIndex = skeleton->GetInverseBindMatricesBufferIndex();
    if (m_currentManager != nullptr) {
        m_currentManager->UpdatePerObjectBuffer(m_perObjectCBView, perObjectCBData);
    }
    skeleton->userIDs.push_back(localID);
}

std::shared_ptr<Skeleton>& RenderableObject::GetSkin() {
	return m_skeleton;
}

PerObjectCB& RenderableObject::GetPerObjectCBData() {
	return perObjectCBData;
}

void RenderableObject::SetCurrentPerObjectCBView(std::unique_ptr<BufferView> view) {
    m_perObjectCBView = std::move(view);
}

std::unique_ptr<BufferView>& RenderableObject::GetCurrentPerObjectCBView() {
	return m_perObjectCBView;
}

void RenderableObject::SetCurrentManager(ObjectManager* manager) {
	m_currentManager = manager;
}

void RenderableObject::SetDrawSetIndex(int index) {
	drawSetIndex = index;
}

int RenderableObject::GetDrawSetIndex() {
	return drawSetIndex;
}