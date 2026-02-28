#include "Mesh/Mesh.h"
#include "Mesh/ClusterLODUtilities.h"

#include <limits>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <bit>
#include <cmath>
#include <array>
#include <cstring>
#include <mutex>
#include <cassert>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/TaskSchedulerManager.h"
#include "Render/PSOFlags.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Materials/Material.h"
#include "Mesh/VertexFlags.h"
#include "Managers/MeshManager.h"
#include "Animation/Skeleton.h"

#include "../shaders/Common/defines.h"

std::atomic<uint32_t> Mesh::globalMeshCount = 0;

namespace
{
	BoundingSphere BuildObjectBoundingSphereFromRootNode(const std::vector<ClusterLODNode>& nodes, uint32_t rootNodeIndex)
	{
		BoundingSphere sphere{};
		if (rootNodeIndex >= nodes.size()) {
			return sphere;
		}

		const ClusterLODTraversalMetric& metric = nodes[rootNodeIndex].traversalMetric;
		sphere.sphere = DirectX::XMFLOAT4(
			metric.boundingSphereX,
			metric.boundingSphereY,
			metric.boundingSphereZ,
			metric.boundingSphereRadius);
		return sphere;
	}
}

std::shared_ptr<Mesh> MeshIngestBuilder::Build(
	const std::shared_ptr<Material>& material,
	std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD,
	MeshCpuDataPolicy cpuDataPolicy) {
	auto vertices = std::make_unique<std::vector<std::byte>>(std::move(m_vertices));

	std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices;
	if (!m_skinningVertices.empty()) {
		skinningVertices = std::make_unique<std::vector<std::byte>>(std::move(m_skinningVertices));
	}

	return Mesh::CreateSharedFromIngest(
		std::move(vertices),
		m_vertexSize,
		std::move(skinningVertices),
		m_skinningVertexSize,
		std::move(m_indices),
		material,
		m_flags,
		std::move(prebuiltClusterLOD),
		cpuDataPolicy);
}

ClusterLODPrebuildArtifacts MeshIngestBuilder::BuildClusterLODArtifacts() const {
	const std::vector<std::byte>* skinningVertices = m_skinningVertices.empty() ? nullptr : &m_skinningVertices;
	return BuildClusterLODArtifactsFromGeometry(
		m_vertices,
		m_vertexSize,
		skinningVertices,
		m_skinningVertexSize,
		m_indices,
		m_flags);
}


Mesh::Mesh(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, unsigned int flags, std::optional<ClusterLODPrebuiltData>&& prebuiltClusterLOD, MeshCpuDataPolicy cpuDataPolicy, bool deferResourceCreation) {
	if (prebuiltClusterLOD.has_value()) {
		m_prebuiltClusterLOD = std::move(prebuiltClusterLOD);
	}
	m_perMeshBufferData.vertexFlags = flags;
	m_perMeshBufferData.vertexByteSize = vertexSize;
	m_perMeshBufferData.numVertices = 0; //static_cast<uint32_t>(m_vertices->size() / vertexSize);
	m_perMeshBufferData.skinningVertexByteSize = skinningVertexSize;

	m_perMeshBufferData.vertexFlags = flags;
	m_perMeshBufferData.vertexByteSize = vertexSize;
	m_perMeshBufferData.numVertices = 0; // static_cast<uint32_t>(m_vertices->size() / vertexSize);
	m_perMeshBufferData.skinningVertexByteSize = skinningVertexSize;
	m_perMeshBufferData.clodMeshletBufferOffset = 0;
	m_perMeshBufferData.clodMeshletVerticesBufferOffset = 0;
	m_perMeshBufferData.clodMeshletTrianglesBufferOffset = 0;
	m_perMeshBufferData.clodNumMeshlets = 0;

	m_skinningVertexSize = skinningVertexSize;
    this->material = material;

	if (!deferResourceCreation) {
		CreateBuffers(indices);

		m_globalMeshID = GetNextGlobalIndex();
	}
	else {
		m_globalMeshID = 0;
	}
}

void Mesh::ClearCLodCacheBuildChunkData(bool shrinkToFit)
{
	auto clearChunkStorage = [shrinkToFit](auto& chunks) {
		chunks.clear();
		if (shrinkToFit) {
			chunks.shrink_to_fit();
		}
	};

	clearChunkStorage(m_clodCacheBuildChunkData.groupVertexChunks);
	clearChunkStorage(m_clodCacheBuildChunkData.groupSkinningVertexChunks);
	clearChunkStorage(m_clodCacheBuildChunkData.groupMeshletVertexChunks);
	clearChunkStorage(m_clodCacheBuildChunkData.groupCompressedPositionWordChunks);
	clearChunkStorage(m_clodCacheBuildChunkData.groupCompressedNormalWordChunks);
	clearChunkStorage(m_clodCacheBuildChunkData.groupCompressedMeshletVertexWordChunks);
	clearChunkStorage(m_clodCacheBuildChunkData.groupMeshletChunks);
	clearChunkStorage(m_clodCacheBuildChunkData.groupMeshletTriangleChunks);
	clearChunkStorage(m_clodCacheBuildChunkData.groupMeshletBoundsChunks);
}

void Mesh::ReleaseCLodChunkUploadData()
{
	ClearCLodCacheBuildChunkData(true);
}

void Mesh::ApplyPrebuiltClusterLODData(const ClusterLODPrebuiltData& data)
{
	const bool hasDiskBackedStreamingSource = !data.groupDiskLocators.empty() && !data.cacheSource.containerFileName.empty();

	m_clodGroups = data.groups;
	m_clodChildren = data.children;
	m_clodGroupChunks = data.groupChunks;
	if (hasDiskBackedStreamingSource && m_clodGroupChunks.size() != m_clodGroups.size()) {
		m_clodGroupChunks.assign(m_clodGroups.size(), {});
		for (size_t groupIndex = 0; groupIndex < m_clodGroups.size(); ++groupIndex) {
			m_clodGroupChunks[groupIndex].groupVertexCount = m_clodGroups[groupIndex].groupVertexCount;
			m_clodGroupChunks[groupIndex].meshletCount = m_clodGroups[groupIndex].meshletCount;
			m_clodGroupChunks[groupIndex].meshletBoundsCount = m_clodGroups[groupIndex].meshletCount;
		}
	}
	ClearCLodCacheBuildChunkData(false);
	m_clodGroupDiskLocators = data.groupDiskLocators;
	m_clodCacheSource = data.cacheSource;
	m_clodNodes = data.nodes;
	m_clodTopRootNode = 0;
	m_perMeshBufferData.boundingSphere = data.objectBoundingSphere;

	m_clodMaxDepth = 0;
	for (const auto& group : m_clodGroups) {
		m_clodMaxDepth = std::max<uint32_t>(m_clodMaxDepth, static_cast<uint32_t>(std::max(group.depth, 0)));
	}
	m_clodLodLevelRoots.resize(m_clodMaxDepth + 1);
	for (uint32_t depth = 0; depth <= m_clodMaxDepth; ++depth) {
		m_clodLodLevelRoots[depth] = 1u + depth;
	}
	m_clodLodNodeRanges.clear();

	m_perMeshBufferData.numVertices = 0; // TODO: Remove, clod doesn't need it

	if (m_perMeshBufferData.boundingSphere.sphere.w <= 0.0f && !m_clodNodes.empty())
	{
		m_perMeshBufferData.boundingSphere = BuildObjectBoundingSphereFromRootNode(m_clodNodes, m_clodTopRootNode);
	}
}

void Mesh::AdoptCLodDiskStreamingMetadata(const ClusterLODPrebuiltData& data)
{
	if (data.groupDiskLocators.empty() || data.cacheSource.containerFileName.empty()) {
		return;
	}

	m_clodGroupDiskLocators = data.groupDiskLocators;
	m_clodCacheSource = data.cacheSource;

	if (m_clodGroupChunks.size() != m_clodGroups.size()) {
		m_clodGroupChunks.assign(m_clodGroups.size(), {});
		for (size_t groupIndex = 0; groupIndex < m_clodGroups.size(); ++groupIndex) {
			m_clodGroupChunks[groupIndex].groupVertexCount = m_clodGroups[groupIndex].groupVertexCount;
			m_clodGroupChunks[groupIndex].meshletCount = m_clodGroups[groupIndex].meshletCount;
			m_clodGroupChunks[groupIndex].meshletBoundsCount = m_clodGroups[groupIndex].meshletCount;
		}
	}

	ClearCLodCacheBuildChunkData(false);
}

ClusterLODPrebuiltData Mesh::GetClusterLODPrebuiltData() const
{
	ClusterLODPrebuiltData out{};
	out.groups = m_clodGroups;
	out.children = m_clodChildren;
	out.objectBoundingSphere = m_perMeshBufferData.boundingSphere;
	out.groupChunks = m_clodGroupChunks;
	out.groupDiskLocators = m_clodGroupDiskLocators;
	out.cacheSource = m_clodCacheSource;
	out.nodes = m_clodNodes;
	return out;
}

ClusterLODCacheBuildPayload Mesh::GetClusterLODCacheBuildPayload() const
{
	ClusterLODCacheBuildPayload payload{};
	payload.groupVertexChunks = &m_clodCacheBuildChunkData.groupVertexChunks;
	payload.groupSkinningVertexChunks = &m_clodCacheBuildChunkData.groupSkinningVertexChunks;
	payload.groupMeshletVertexChunks = &m_clodCacheBuildChunkData.groupMeshletVertexChunks;
	payload.groupCompressedPositionWordChunks = &m_clodCacheBuildChunkData.groupCompressedPositionWordChunks;
	payload.groupCompressedNormalWordChunks = &m_clodCacheBuildChunkData.groupCompressedNormalWordChunks;
	payload.groupCompressedMeshletVertexWordChunks = &m_clodCacheBuildChunkData.groupCompressedMeshletVertexWordChunks;
	payload.groupMeshletChunks = &m_clodCacheBuildChunkData.groupMeshletChunks;
	payload.groupMeshletTriangleChunks = &m_clodCacheBuildChunkData.groupMeshletTriangleChunks;
	payload.groupMeshletBoundsChunks = &m_clodCacheBuildChunkData.groupMeshletBoundsChunks;
	return payload;
}

ClusterLODCacheBuildOwnedData Mesh::GetClusterLODCacheBuildOwnedData() const
{
	ClusterLODCacheBuildOwnedData owned{};
	owned.groupVertexChunks = m_clodCacheBuildChunkData.groupVertexChunks;
	owned.groupSkinningVertexChunks = m_clodCacheBuildChunkData.groupSkinningVertexChunks;
	owned.groupMeshletVertexChunks = m_clodCacheBuildChunkData.groupMeshletVertexChunks;
	owned.groupCompressedPositionWordChunks = m_clodCacheBuildChunkData.groupCompressedPositionWordChunks;
	owned.groupCompressedNormalWordChunks = m_clodCacheBuildChunkData.groupCompressedNormalWordChunks;
	owned.groupCompressedMeshletVertexWordChunks = m_clodCacheBuildChunkData.groupCompressedMeshletVertexWordChunks;
	owned.groupMeshletChunks = m_clodCacheBuildChunkData.groupMeshletChunks;
	owned.groupMeshletTriangleChunks = m_clodCacheBuildChunkData.groupMeshletTriangleChunks;
	owned.groupMeshletBoundsChunks = m_clodCacheBuildChunkData.groupMeshletBoundsChunks;
	return owned;
}

void Mesh::CreateBuffers(const std::vector<UINT32>& indices) {
	if (m_prebuiltClusterLOD.has_value()) {
		ApplyPrebuiltClusterLODData(m_prebuiltClusterLOD.value());
		m_prebuiltClusterLOD.reset();
	} else {
		throw std::runtime_error("Mesh was created without prebuilt ClusterLOD data");
	}
}

int Mesh::GetNextGlobalIndex() {
    return globalMeshCount.fetch_add(1, std::memory_order_relaxed);
}

uint64_t Mesh::GetGlobalID() const {
	return m_globalMeshID;
}

void Mesh::SetPreSkinningVertexBufferView(std::unique_ptr<BufferView> view) {
	m_preSkinningVertexBufferView = std::move(view);
	if (m_preSkinningVertexBufferView != nullptr) {
		m_perMeshBufferData.vertexBufferOffset = static_cast<uint32_t>(m_preSkinningVertexBufferView->GetOffset()); // TODO: Vertex buffer pool instead of one buffer limited to uint32 max
	}

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetPostSkinningVertexBufferView(std::unique_ptr<BufferView> view) {
	m_postSkinningVertexBufferView = std::move(view);
	if (m_postSkinningVertexBufferView != nullptr) {
		m_perMeshBufferData.vertexBufferOffset = static_cast<uint32_t>(m_postSkinningVertexBufferView->GetOffset()); // TODO: Vertex buffer pool instead of one buffer limited to uint32 max
	}

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

BufferView* Mesh::GetPreSkinningVertexBufferView() {
	return m_preSkinningVertexBufferView.get();
}

BufferView* Mesh::GetPostSkinningVertexBufferView() {
	return m_postSkinningVertexBufferView.get();
}

void Mesh::SetCLodBufferViews(
	std::unique_ptr<BufferView> clusterLODGroupsView,
	std::unique_ptr<BufferView> clusterLODChildrenView,
	std::unique_ptr<BufferView> clusterLODNodesView
) {
	m_clusterLODGroupsView = std::move(clusterLODGroupsView);
	m_clusterLODChildrenView = std::move(clusterLODChildrenView);
	m_clusterLODNodesView = std::move(clusterLODNodesView);

	auto firstChunkOffsetDiv = [](const auto& chunkViews, uint32_t divisor) -> uint32_t {
		uint32_t minGroupIndex = std::numeric_limits<uint32_t>::max();
		uint64_t selectedOffset = 0u;
		bool found = false;
		for (const auto& [groupIndex, chunkView] : chunkViews) {
			if (chunkView != nullptr && groupIndex < minGroupIndex) {
				minGroupIndex = groupIndex;
				selectedOffset = chunkView->GetOffset();
				found = true;
			}
		}
		if (found) {
			return static_cast<uint32_t>(selectedOffset / divisor);
		}
		return 0u;
	};
	auto firstChunkOffsetBytes = [](const auto& chunkViews) -> uint32_t {
		uint32_t minGroupIndex = std::numeric_limits<uint32_t>::max();
		uint64_t selectedOffset = 0u;
		bool found = false;
		for (const auto& [groupIndex, chunkView] : chunkViews) {
			if (chunkView != nullptr && groupIndex < minGroupIndex) {
				minGroupIndex = groupIndex;
				selectedOffset = chunkView->GetOffset();
				found = true;
			}
		}
		if (found) {
			return static_cast<uint32_t>(selectedOffset);
		}
		return 0u;
	};

	m_perMeshBufferData.clodMeshletBufferOffset = firstChunkOffsetDiv(m_clodMeshletChunkViews, sizeof(meshopt_Meshlet));
	m_perMeshBufferData.clodMeshletVerticesBufferOffset = firstChunkOffsetDiv(m_clodMeshletVertexChunkViews, sizeof(uint32_t));
	m_perMeshBufferData.clodMeshletTrianglesBufferOffset = firstChunkOffsetBytes(m_clodMeshletTriangleChunkViews); // Intentionally in bytes to index byteaddressbuffer
	uint32_t totalMeshletCount = 0;
	for (const auto& group : m_clodGroups) {
		totalMeshletCount += group.meshletCount;
	}
	m_perMeshBufferData.clodNumMeshlets = totalMeshletCount;
	if (m_pCurrentMeshManager != nullptr && m_perMeshBufferView != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetBaseSkin(std::shared_ptr<Skeleton> skeleton) {
	m_baseSkeleton = skeleton;
}

void Mesh::SetMaterialDataIndex(unsigned int index) {
	m_perMeshBufferData.materialDataIndex = index;
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}
