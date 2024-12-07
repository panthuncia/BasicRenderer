#pragma once

#include <vector>
#include <string>
#include <memory>

#include "SceneNode.h"
#include "Mesh.h"
#include "Buffers.h"
#include "ResourceHandles.h"
#include "Skeleton.h"
#include "BufferView.h"

class ObjectManager;

class RenderableObject : public SceneNode {
public:
	RenderableObject(std::wstring name);
	RenderableObject(std::wstring name, std::vector<std::shared_ptr<Mesh>> meshes);
	RenderableObject(std::wstring name, std::vector<std::shared_ptr<Mesh>>& newOpaqueMeshes, std::vector<std::shared_ptr<Mesh>>& newTransparentMeshes);
	std::vector<std::shared_ptr<Mesh>>& GetOpaqueMeshes();
	std::vector<std::shared_ptr<Mesh>>& GetTransparentMeshes();
	bool HasTransparent() const;
	bool HasOpaque() const;
	void SetSkin(std::shared_ptr<Skeleton> skeleton);
	std::shared_ptr<Skeleton>& GetSkin();
	PerObjectCB& GetPerObjectCBData();
	void SetCurrentPerObjectCBView(std::shared_ptr<BufferView>& view);
	std::shared_ptr<BufferView>& GetCurrentPerObjectCBView();

	void SetCurrentManager(ObjectManager* manager);
	void SetCurrentOpaqueDrawSetIndices(const std::vector<unsigned int>& indices);
	void SetCurrentTransparentDrawSetIndices(const std::vector<unsigned int>& indices);
	void SetCurrentOpaqueDrawSetCommandViews(const std::vector<std::shared_ptr<BufferView>>& views);
	void SetCurrentTransparentDrawSetCommandViews(const std::vector<std::shared_ptr<BufferView>>& views);
	std::vector<unsigned int>& GetCurrentOpaqueDrawSetIndices();
	std::vector<unsigned int>& GetCurrentTransparentDrawSetIndices();
	std::vector<std::shared_ptr<BufferView>>& GetCurrentOpaqueDrawSetCommandViews();
	std::vector<std::shared_ptr<BufferView>>& GetCurrentTransparentDrawSetCommandViews();
	int m_fileLocalSkinIndex = -1; // hack for loading gltf. TODO: remove
private:
	void UpdateBuffers();
	std::vector<std::shared_ptr<Mesh>> opaqueMeshes;
	std::vector<std::shared_ptr<Mesh>> transparentMeshes;
	std::vector<unsigned int> m_opaqueDrawSetIndices;
	std::vector<unsigned int> m_transparentDrawSetIndices;
	std::vector<std::shared_ptr<BufferView>> m_opaqueDrawSetCommandViews;
	std::vector<std::shared_ptr<BufferView>> m_transparentDrawSetCommandViews;
	PerObjectCB perObjectCBData;
	bool m_hasTransparent = false;
	bool m_hasOpaque = false;
	std::shared_ptr<Skeleton> m_skeleton = nullptr;
	std::shared_ptr<BufferView> m_perObjectCBView;
	ObjectManager* m_currentManager = nullptr;
protected:
	void OnUpdate() override;
};