#pragma once

#include <memory>
#include <d3d12.h>
#include <wrl/client.h>

#include "DynamicBuffer.h"
#include "ResourceHandles.h"
#include "BufferView.h"

class Mesh;

class MeshManager {
public:
	static std::unique_ptr<MeshManager> CreateUnique() {
		return std::unique_ptr<MeshManager>(new MeshManager());
	}
	void AddMesh(std::shared_ptr<Mesh>& mesh);
	void RemoveMesh(std::shared_ptr<BufferView> view);
private:
	MeshManager();
	DynamicBufferHandle m_vertices;
	DynamicBufferHandle m_meshletOffsets;
	DynamicBufferHandle m_meshletIndices;
	DynamicBufferHandle m_meshletTriangles;
};