#pragma once

#include <vector>
#include <string>

#include "SceneNode.h"
#include "Mesh.h"
#include "Buffers.h"

class RenderableObject : public SceneNode {
public:
	RenderableObject(std::string name);
	RenderableObject(std::string name, std::vector<Mesh> meshes);
	std::vector<Mesh>& getMeshes();
	ComPtr<ID3D12Resource>& getConstantBuffer();
private:
	void CreateBuffers();
	void UpdateBuffers();
	std::vector<Mesh> meshes;
	ComPtr<ID3D12Resource> perMeshConstantBuffer;
	UINT8* pPerMeshConstantBuffer;
	PerMeshCB perMeshCBData;
protected:
	void onUpdate() override;
};