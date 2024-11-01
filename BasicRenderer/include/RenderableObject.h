#pragma once

#include <vector>
#include <string>

#include "SceneNode.h"
#include "Mesh.h"
#include "Buffers.h"
#include "ResourceHandles.h"
#include "Skeleton.h"

class RenderableObject : public SceneNode {
public:
	RenderableObject(std::wstring name);
	RenderableObject(std::wstring name, std::vector<std::shared_ptr<Mesh>> meshes);
	RenderableObject(std::wstring name, std::vector<std::shared_ptr<Mesh>>& newOpaqueMeshes, std::vector<std::shared_ptr<Mesh>>& newTransparentMeshes);
	std::vector<std::shared_ptr<Mesh>>& GetOpaqueMeshes();
	std::vector<std::shared_ptr<Mesh>>& GetTransparentMeshes();
	bool HasTransparent() const;
	bool HasOpaque() const;
	BufferHandle& GetConstantBuffer();
	void SetSkin(std::shared_ptr<Skeleton> skeleton);
	std::shared_ptr<Skeleton>& GetSkin();
	int m_fileLocalSkinIndex = -1; // hack for loading gltf. TODO: remove
private:
	void CreateBuffers();
	void UpdateBuffers();
	std::vector<std::shared_ptr<Mesh>> opaqueMeshes;
	std::vector<std::shared_ptr<Mesh>> transparentMeshes;
	BufferHandle perObjectConstantBuffer;
	UINT8* pPerObjectConstantBuffer;
	PerObjectCB perObjectCBData;
	bool m_hasTransparent = false;
	bool m_hasOpaque = false;
	std::shared_ptr<Skeleton> m_skeleton = nullptr;
protected:
	void OnUpdate() override;
};