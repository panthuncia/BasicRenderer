#pragma once

#include <memory>
#include <d3d12.h>
#include <wrl/client.h>

#include "DynamicBuffer.h"
#include "ResourceHandles.h"

class MeshManager {
public:
	std::unique_ptr<MeshManager> CreateUnique() {
		return std::unique_ptr<MeshManager>(new MeshManager());
	}
private:
	MeshManager();
	DynamicBufferHandle m_vertices;
	DynamicBufferHandle m_meshletOffsets;
	DynamicBufferHandle m_meshletIndices;
	DynamicBufferHandle m_meshletTriangles;
};