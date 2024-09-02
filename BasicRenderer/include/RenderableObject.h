#pragma once

#include <vector>
#include <string>

#include "SceneNode.h"
#include "Mesh.h"
#include "Buffers.h"
#include "Skeleton.h"

class RenderableObject : public SceneNode {
public:
	RenderableObject(std::string name);
	RenderableObject(std::string name, std::vector<Mesh> meshes);
	std::vector<Mesh>& GetOpaqueMeshes();
	std::vector<Mesh>& GetTransparentMeshes();
	bool HasTransparent() const;
	bool HasOpaque() const;
	ComPtr<ID3D12Resource>& GetConstantBuffer();
	void SetSkin(std::shared_ptr<Skeleton> skeleton);
	int m_fileLocalSkinIndex = -1; // hack for loading gltf. TODO: remove
private:
	void CreateBuffers();
	void UpdateBuffers();
	std::vector<Mesh> opaqueMeshes;
	std::vector<Mesh> transparentMeshes;
	ComPtr<ID3D12Resource> perObjectConstantBuffer;
	UINT8* pPerObjectConstantBuffer;
	PerObjectCB perObjectCBData;
	bool m_hasTransparent = false;
	bool m_hasOpaque = false;
	std::shared_ptr<Skeleton> m_skeleton = nullptr;
protected:
	void OnUpdate() override;
};