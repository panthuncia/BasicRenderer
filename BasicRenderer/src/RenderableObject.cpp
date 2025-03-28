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
		auto instance = MeshInstance::CreateShared(mesh);
		if (mesh->HasBaseSkin()) {
			m_hasSkinned = true;
			//instance->SetSkeleton(mesh->GetBaseSkin());
		}
        switch (mesh->material->m_blendState) {
		case BlendState::BLEND_STATE_OPAQUE:
			opaqueMeshes.push_back(instance);
			m_hasOpaque = true;
			break;
		case BlendState::BLEND_STATE_MASK:
			alphaTestMeshes.push_back(instance);
			m_hasAlphaTest = true;
			break;
		case BlendState::BLEND_STATE_BLEND:
			blendMeshes.push_back(instance);
			m_hasBlend = true;
			break;
        }
    }
}

RenderableObject::RenderableObject(std::wstring name, std::vector<std::shared_ptr<MeshInstance>> meshes) : SceneNode(name) {
	for (auto& mesh : meshes) {

		auto instance = MeshInstance::CreateShared(mesh->GetMesh());
		if (mesh->GetMesh()->HasBaseSkin()) {
			m_hasSkinned = true;
			//instance->SetSkeleton(mesh->GetSkin());
		}
		switch (mesh->GetMesh()->material->m_blendState) {
		case BlendState::BLEND_STATE_OPAQUE:
			opaqueMeshes.push_back(instance);
			m_hasOpaque = true;
			break;
		case BlendState::BLEND_STATE_MASK:
			alphaTestMeshes.push_back(instance);
			m_hasAlphaTest = true;
			break;
		case BlendState::BLEND_STATE_BLEND:
			blendMeshes.push_back(instance);
			m_hasBlend = true;
			break;
		}
	}
}

RenderableObject::RenderableObject(std::wstring name, std::vector<std::shared_ptr<MeshInstance>>& newOpaqueMeshes, std::vector<std::shared_ptr<MeshInstance>>& newAlphaTestMeshes, std::vector<std::shared_ptr<MeshInstance>>& newBlendMeshes) : SceneNode(name) {
    if (newOpaqueMeshes.size() > 0) {
        m_hasOpaque = true;
        for (auto& mesh : newOpaqueMeshes) {
			auto instance = MeshInstance::CreateShared(mesh->GetMesh());
			if (mesh->GetMesh()->HasBaseSkin()) {
				m_hasSkinned = true;
				//instance->SetSkeleton(mesh->GetSkin());
			}
			opaqueMeshes.push_back(instance);
		}
    }
    if (newAlphaTestMeshes.size() > 0) {
        m_hasAlphaTest = true;
        for (auto& mesh : newAlphaTestMeshes) {
			auto instance = MeshInstance::CreateShared(mesh->GetMesh());
			if (mesh->GetMesh()->HasBaseSkin()) {
				m_hasSkinned = true;
				//instance->SetSkeleton(mesh->GetSkin());
			}
			alphaTestMeshes.push_back(instance);
		}
    }
	if (newBlendMeshes.size() > 0) {
		m_hasBlend = true;
		for (auto& mesh : newBlendMeshes) {
			auto instance = MeshInstance::CreateShared(mesh->GetMesh());
			if (mesh->GetMesh()->HasBaseSkin()) {
				m_hasSkinned = true;
				//instance->SetSkeleton(mesh->GetSkin());
			}
			blendMeshes.push_back(instance);
		}
	}
}

std::vector<std::shared_ptr<MeshInstance>>& RenderableObject::GetOpaqueMeshes() {
	return opaqueMeshes;
}

std::vector<std::shared_ptr<MeshInstance>>& RenderableObject::GetAlphaTestMeshes() {
    return alphaTestMeshes;
}

std::vector<std::shared_ptr<MeshInstance>>& RenderableObject::GetBlendMeshes() {
	return blendMeshes;
}

bool RenderableObject::HasOpaque() const {
    return m_hasOpaque;
}

bool RenderableObject::HasAlphaTest() const {
    return m_hasAlphaTest;
}

bool RenderableObject::HasBlend() const {
	return m_hasBlend;
}

bool RenderableObject::HasSkinned() const {
	return m_hasSkinned;
}

void RenderableObject::UpdateBuffers() {
    perObjectCBData.modelMatrix = transform.modelMatrix;

    XMMATRIX upperLeft3x3 = XMMatrixSet(
        XMVectorGetX(perObjectCBData.modelMatrix.r[0]), XMVectorGetY(perObjectCBData.modelMatrix.r[0]), XMVectorGetZ(perObjectCBData.modelMatrix.r[0]), 0.0f,
        XMVectorGetX(perObjectCBData.modelMatrix.r[1]), XMVectorGetY(perObjectCBData.modelMatrix.r[1]), XMVectorGetZ(perObjectCBData.modelMatrix.r[1]), 0.0f,
        XMVectorGetX(perObjectCBData.modelMatrix.r[2]), XMVectorGetY(perObjectCBData.modelMatrix.r[2]), XMVectorGetZ(perObjectCBData.modelMatrix.r[2]), 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );

    m_currentManager->UpdatePerObjectBuffer(m_perObjectCBView.get(), perObjectCBData);

	// TODO: Any way to do this math without transforming to 4x4 and then back?
	//XMFLOAT3X3 normalMat = GetUpperLeft3x3(XMMatrixTranspose(XMMatrixInverse(nullptr, upperLeft3x3)));
	XMMATRIX normalMat = XMMatrixInverse(nullptr, upperLeft3x3);
	m_currentManager->UpdateNormalMatrixBuffer(normalMatrixView.get(), &normalMat);
	
}

void RenderableObject::OnUpdate() {
    UpdateBuffers();
}

PerObjectCB& RenderableObject::GetPerObjectCBData() {
	return perObjectCBData;
}

void RenderableObject::SetCurrentPerObjectCBView(std::shared_ptr<BufferView>& view) {
    m_perObjectCBView = view;
}

std::shared_ptr<BufferView>& RenderableObject::GetCurrentPerObjectCBView() {
	return m_perObjectCBView;
}

void RenderableObject::SetCurrentManager(ObjectManager* manager) {
	m_currentManager = manager;
}

void RenderableObject::SetCurrentOpaqueDrawSetIndices(const std::vector<unsigned int>& indices) {
	m_opaqueDrawSetIndices = indices;
}

std::vector<unsigned int>& RenderableObject::GetCurrentOpaqueDrawSetIndices() {
	return m_opaqueDrawSetIndices;
}

void RenderableObject::SetCurrentAlphaTestDrawSetIndices(const std::vector<unsigned int>& indices) {
	m_alphaTestDrawSetIndices = indices;
}

std::vector<unsigned int>& RenderableObject::GetCurrentAlphaTestDrawSetIndices() {
	return m_alphaTestDrawSetIndices;
}

void RenderableObject::SetCurrentBlendDrawSetIndices(const std::vector<unsigned int>& indices) {
	m_blendDrawSetIndices = indices;
}

std::vector<unsigned int>& RenderableObject::GetCurrentBlendDrawSetIndices() {
	return m_blendDrawSetIndices;
}

void RenderableObject::SetCurrentOpaqueDrawSetCommandViews(const std::vector<std::shared_ptr<BufferView>>& views) {
	m_opaqueDrawSetCommandViews = views;
}

void RenderableObject::SetCurrentAlphaTestDrawSetCommandViews(const std::vector<std::shared_ptr<BufferView>>& views) {
	m_alphaTestDrawSetCommandViews = views;
}

void RenderableObject::SetCurrentBlendDrawSetCommandViews(const std::vector<std::shared_ptr<BufferView>>& views) {
	m_blendDrawSetCommandViews = views;
}

std::vector<std::shared_ptr<BufferView>>& RenderableObject::GetCurrentOpaqueDrawSetCommandViews() {
	return m_opaqueDrawSetCommandViews;
}

std::vector<std::shared_ptr<BufferView>>& RenderableObject::GetCurrentAlphaTestDrawSetCommandViews() {
	return m_alphaTestDrawSetCommandViews;
}

std::vector<std::shared_ptr<BufferView>>& RenderableObject::GetCurrentBlendDrawSetCommandViews() {
	return m_blendDrawSetCommandViews;
}

void RenderableObject::SetNormalMatrixView(std::shared_ptr<BufferView> view) {
	normalMatrixView = view;
	perObjectCBData.normalMatrixBufferIndex = normalMatrixView->GetOffset() / sizeof(XMFLOAT4X4);
	m_currentManager->UpdatePerObjectBuffer(m_perObjectCBView.get(), perObjectCBData);
}

BufferView* RenderableObject::GetNormalMatrixView() {
	return normalMatrixView.get();
}

void RenderableObject::SetAnimationSpeed(float speed) {
	m_animationSpeed = speed;
	for (auto& mesh : opaqueMeshes) {
		mesh->SetAnimationSpeed(speed);
	}
	for (auto& mesh : alphaTestMeshes) {
		mesh->SetAnimationSpeed(speed);
	}
	for (auto& mesh : blendMeshes) {
		mesh->SetAnimationSpeed(speed);
	}
}

float RenderableObject::GetAnimationSpeed() {
	return m_animationSpeed;
}