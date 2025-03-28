#pragma once

#include <vector>
#include <string>
#include <memory>
#include <optional>

#include "SceneNode.h"
#include "Mesh.h"
#include "Buffers.h"
#include "ResourceHandles.h"
#include "Skeleton.h"
#include "BufferView.h"
#include "MeshInstance.h"

class ObjectManager;

class RenderableObject : public SceneNode {
public:
	RenderableObject(std::wstring name);
	RenderableObject(std::wstring name, std::vector<std::shared_ptr<Mesh>> meshes);
	RenderableObject(std::wstring name, std::vector<std::shared_ptr<MeshInstance>> meshes);
	RenderableObject(std::wstring name, std::vector<std::shared_ptr<MeshInstance>>& newOpaqueMeshes, std::vector<std::shared_ptr<MeshInstance>>& newAlphaTestMeshes, std::vector<std::shared_ptr<MeshInstance>>& newBlendMeshes);
	std::vector<std::shared_ptr<MeshInstance>>& GetOpaqueMeshes();
	std::vector<std::shared_ptr<MeshInstance>>& GetAlphaTestMeshes();
	std::vector<std::shared_ptr<MeshInstance>>& GetBlendMeshes();
	bool HasAlphaTest() const;
	bool HasOpaque() const;
	bool HasBlend() const;
	bool HasSkinned() const;
	PerObjectCB& GetPerObjectCBData();
	void SetCurrentPerObjectCBView(std::shared_ptr<BufferView>& view);
	std::shared_ptr<BufferView>& GetCurrentPerObjectCBView();

	void SetCurrentManager(ObjectManager* manager);
	void SetCurrentOpaqueDrawSetIndices(const std::vector<unsigned int>& indices);
	void SetCurrentAlphaTestDrawSetIndices(const std::vector<unsigned int>& indices);
	void SetCurrentBlendDrawSetIndices(const std::vector<unsigned int>& indices);
	void SetCurrentOpaqueDrawSetCommandViews(const std::vector<std::shared_ptr<BufferView>>& views);
	void SetCurrentAlphaTestDrawSetCommandViews(const std::vector<std::shared_ptr<BufferView>>& views);
	void SetCurrentBlendDrawSetCommandViews(const std::vector<std::shared_ptr<BufferView>>& indices);
	std::vector<unsigned int>& GetCurrentOpaqueDrawSetIndices();
	std::vector<unsigned int>& GetCurrentAlphaTestDrawSetIndices();
	std::vector<unsigned int>& GetCurrentBlendDrawSetIndices();
	std::vector<std::shared_ptr<BufferView>>& GetCurrentOpaqueDrawSetCommandViews();
	std::vector<std::shared_ptr<BufferView>>& GetCurrentAlphaTestDrawSetCommandViews();
	std::vector<std::shared_ptr<BufferView>>& GetCurrentBlendDrawSetCommandViews();
	void SetNormalMatrixView(std::shared_ptr<BufferView> view);
	BufferView* GetNormalMatrixView();
	void SetAnimationSpeed(float speed);
	float GetAnimationSpeed();
	int m_fileLocalSkinIndex = -1; // hack for loading gltf. TODO: remove
private:
	void UpdateBuffers();
	std::vector<std::shared_ptr<MeshInstance>> opaqueMeshes;
	std::vector<std::shared_ptr<MeshInstance>> alphaTestMeshes;
	std::vector<std::shared_ptr<MeshInstance>> blendMeshes;
	std::vector<unsigned int> m_opaqueDrawSetIndices;
	std::vector<unsigned int> m_alphaTestDrawSetIndices;
	std::vector<unsigned int> m_blendDrawSetIndices;
	std::vector<std::shared_ptr<BufferView>> m_opaqueDrawSetCommandViews;
	std::vector<std::shared_ptr<BufferView>> m_alphaTestDrawSetCommandViews;
	std::vector<std::shared_ptr<BufferView>> m_blendDrawSetCommandViews;
	std::shared_ptr<BufferView> normalMatrixView = nullptr;
	PerObjectCB perObjectCBData;
	bool m_hasAlphaTest = false;
	bool m_hasOpaque = false;
	bool m_hasBlend = false;
	bool m_hasSkinned = false;
	std::shared_ptr<Skeleton> m_skeleton = nullptr;
	std::shared_ptr<BufferView> m_perObjectCBView;
	ObjectManager* m_currentManager = nullptr;
	float m_animationSpeed = 1.0f;
protected:
	void OnUpdate() override;
};