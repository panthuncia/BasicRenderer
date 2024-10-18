#include "MeshManager.h"

#include "ResourceManager.h"
#include "ResourceStates.h"

MeshManager::MeshManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_vertices = resourceManager.CreateIndexedDynamicBuffer(1, ResourceState::ALL_SRV, L"vertices");
	m_meshletOffsets = resourceManager.CreateIndexedDynamicBuffer(1, ResourceState::ALL_SRV, L"meshletOffsets");
	m_meshletIndices = resourceManager.CreateIndexedDynamicBuffer(1, ResourceState::ALL_SRV, L"meshletIndices");
	m_meshletTriangles = resourceManager.CreateIndexedDynamicBuffer(1, ResourceState::ALL_SRV, L"meshletTriangles");

}