#include "Mesh/Mesh.h"

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Render/PSOFlags.h"
#include "Managers/Singletons/ResourceManager.h"
#include "Materials/Material.h"
#include "Mesh/VertexFlags.h"
#include "Managers/MeshManager.h"
#include "Animation/Skeleton.h"

#include "../shaders/Common/defines.h"

std::atomic<uint32_t> Mesh::globalMeshCount = 0;

Mesh::Mesh(std::unique_ptr<std::vector<std::byte>> vertices, unsigned int vertexSize, std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices, unsigned int skinningVertexSize, const std::vector<UINT32>& indices, const std::shared_ptr<Material> material, unsigned int flags) {
    m_vertices = std::move(vertices);
	if (skinningVertices.has_value()) {
		m_skinningVertices = std::move(skinningVertices.value());
	}
	m_perMeshBufferData.vertexFlags = flags;
	m_perMeshBufferData.vertexByteSize = vertexSize;
	m_perMeshBufferData.numVertices = static_cast<uint32_t>(m_vertices->size() / vertexSize);
	m_perMeshBufferData.skinningVertexByteSize = skinningVertexSize;

	m_skinningVertexSize = skinningVertexSize;
	CreateBuffers(indices);
    this->material = material;

	m_globalMeshID = GetNextGlobalIndex();
}

void Mesh::CreateVertexBuffer() {
    
    const UINT vertexBufferSize = static_cast<UINT>(m_vertices->size());

	m_vertexBufferHandle = Buffer::CreateShared(rhi::HeapType::DeviceLocal, vertexBufferSize);
	BUFFER_UPLOAD(m_vertices->data(), vertexBufferSize, UploadManager::UploadTarget::FromShared(m_vertexBufferHandle), 0);

    m_vertexBufferView.buffer = m_vertexBufferHandle->GetAPIResource().GetHandle();
    m_vertexBufferView.stride = sizeof(m_perMeshBufferData.vertexByteSize);
    m_vertexBufferView.sizeBytes = vertexBufferSize;
}

void Mesh::BuildClusterLOD(const std::vector<UINT32>& indices)
{
	// Clear any previous data
	m_clodGroups.clear();
	m_clodMeshlets.clear();
	m_clodMeshletVertices.clear();
	m_clodMeshletTriangles.clear();
	m_clodMeshletBounds.clear();
	m_clodMeshletRefinedGroup.clear();
	m_clodRootGroup = 0;
	m_clodChildren.clear();
	m_clodChildLocalMeshletIndices.clear();

	// Meshoptimizer clusterlod expects unsigned int indices
	static_assert(sizeof(UINT32) == sizeof(unsigned int), "UINT32 must be 32-bit");
	const unsigned int* idx = reinterpret_cast<const unsigned int*>(indices.data());

	const size_t vertexStrideBytes = m_perMeshBufferData.vertexByteSize;
	const size_t globalVertexCount = m_vertices->size() / vertexStrideBytes;

	// -------- clod input --------
	clodMesh mesh{};
	mesh.indices = idx;
	mesh.index_count = indices.size();
	mesh.vertex_count = globalVertexCount;

	// Assumes position is first float3 in packed vertex structure
	mesh.vertex_positions = reinterpret_cast<const float*>(m_vertices->data());
	mesh.vertex_positions_stride = vertexStrideBytes;

	// Prototype: positions-only simplification (no attributes)
	mesh.vertex_attributes = nullptr;
	mesh.vertex_attributes_stride = 0;
	mesh.vertex_lock = nullptr;
	mesh.attribute_weights = nullptr;
	mesh.attribute_count = 0;
	mesh.attribute_protect_mask = 0;

	// clod config 
	clodConfig config = clodDefaultConfig(/*max_triangles=*/MS_MESHLET_SIZE);

	config.max_vertices = MS_MESHLET_SIZE;
	config.max_triangles = MS_MESHLET_SIZE;
	config.min_triangles = MS_MESHLET_MIN_SIZE;

	// Keep behavior close to current meshopt_buildMeshletsSpatial usage
	config.cluster_spatial = true;
	config.cluster_fill_weight = 0.5f; // TODO: ?
	config.partition_spatial = true;
	config.optimize_clusters = true;

	// Compute per-cluster bounds from actual triangles for refined clusters
	// (nice for culling, slightly more CPU time)
	config.optimize_bounds = true;

	uint32_t MaxChildren = 32; // TODO: Configurability
	config.partition_size = std::max<size_t>(1, (MaxChildren * 3) / 4);
	config.partition_sort = true; // optional, tends to help spatial coherence

	// We'll treat every produced cluster as a "meshlet" and keep group ranges separately.
	int32_t lastGroupId = -1;

	unsigned int maxChildren = 0;

	clodBuild(config, mesh,
		[&](clodGroup group, const clodCluster* clusters, size_t cluster_count) -> int
		{
			const uint32_t firstMeshlet = static_cast<uint32_t>(m_clodMeshlets.size());
			const uint32_t meshletCount = static_cast<uint32_t>(cluster_count);

			const int32_t groupId = static_cast<int32_t>(m_clodGroups.size());

			ClusterLODGroup outGroup{};
			outGroup.bounds = group.simplified;
			outGroup.firstMeshlet = firstMeshlet;
			outGroup.meshletCount = meshletCount;
			outGroup.depth = group.depth;

			// We'll fill these after we build child buckets
			outGroup.firstChild = 0;
			outGroup.childCount = 0;

			m_clodGroups.push_back(outGroup);

			// build per-group child buckets
			struct ChildBucket
			{
				int32_t refinedGroup = -1;
				std::vector<uint16_t> locals; // meshlet local indices within this group
			};

			std::vector<ChildBucket> buckets;
			buckets.reserve(cluster_count);

			auto add_to_bucket = [&](int32_t refined, uint16_t localIndex)
				{
					for (auto& b : buckets)
					{
						if (b.refinedGroup == refined)
						{
							b.locals.push_back(localIndex);
							return;
						}
					}
					ChildBucket nb;
					nb.refinedGroup = refined;
					nb.locals.reserve(8);
					nb.locals.push_back(localIndex);
					buckets.push_back(std::move(nb));
				};

			for (size_t i = 0; i < cluster_count; ++i)
			{
				const clodCluster& c = clusters[i];
				const size_t triCount = c.index_count / 3;

				// bucket this meshlet by refined group id (can be -1)
				add_to_bucket(static_cast<int32_t>(c.refined), static_cast<uint16_t>(i));

				std::vector<unsigned int> localVerts;
				std::vector<unsigned char> localTris;
				localVerts.resize(c.vertex_count);
				localTris.resize(c.index_count);

				const size_t unique = clodLocalIndices(
					localVerts.data(),
					localTris.data(),
					c.indices,
					c.index_count);

				assert(unique == c.vertex_count);

				meshopt_Meshlet ml{};
				ml.vertex_offset = static_cast<unsigned int>(m_clodMeshletVertices.size());
				ml.triangle_offset = static_cast<unsigned int>(m_clodMeshletTriangles.size());
				ml.vertex_count = static_cast<unsigned int>(unique);
				ml.triangle_count = static_cast<unsigned int>(triCount);

				m_clodMeshletVertices.insert(m_clodMeshletVertices.end(), localVerts.begin(), localVerts.end());
				m_clodMeshletTriangles.insert(m_clodMeshletTriangles.end(), localTris.begin(), localTris.end());

				const unsigned int* vertsPtr = reinterpret_cast<const unsigned int*>(
					m_clodMeshletVertices.data() + ml.vertex_offset);
				const unsigned char* trisPtr = reinterpret_cast<const unsigned char*>(
					m_clodMeshletTriangles.data() + ml.triangle_offset);

				meshopt_Bounds b = meshopt_computeMeshletBounds(
					vertsPtr,
					trisPtr,
					ml.triangle_count,
					reinterpret_cast<const float*>(m_vertices->data()),
					globalVertexCount,
					vertexStrideBytes);

				BoundingSphere sphere{};
				sphere.sphere = DirectX::XMFLOAT4(b.center[0], b.center[1], b.center[2], b.radius);

				m_clodMeshlets.push_back(ml);
				m_clodMeshletBounds.push_back(sphere);
				m_clodMeshletRefinedGroup.push_back(static_cast<int32_t>(c.refined));
			}

			// finalize child table for this group
			ClusterLODGroup& grp = m_clodGroups.back();
			grp.firstChild = static_cast<uint32_t>(m_clodChildren.size());
			grp.childCount = static_cast<uint32_t>(buckets.size());

			if (grp.childCount > maxChildren) {
				maxChildren = grp.childCount;
			}

			for (const auto& b : buckets)
			{
				ClusterLODChild child{};
				child.refinedGroup = b.refinedGroup;
				child.firstLocalMeshletIndex = static_cast<uint32_t>(m_clodChildLocalMeshletIndices.size());
				child.localMeshletCount = static_cast<uint16_t>(b.locals.size());

				m_clodChildren.push_back(child);

				m_clodChildLocalMeshletIndices.insert(
					m_clodChildLocalMeshletIndices.end(),
					b.locals.begin(),
					b.locals.end());
			}

			lastGroupId = groupId;
			return groupId;
		});

	if (maxChildren > MaxChildren) { // TODO: Is this possible?
		throw std::runtime_error("Exceeded maximum allowed Cluster LOD children per group");
	}
	// Root/coarsest group is the last one emitted for this build
	m_clodRootGroup = (lastGroupId >= 0) ? static_cast<uint32_t>(lastGroupId) : 0;
}


void Mesh::CreateMeshlets(const std::vector<UINT32>& indices) {
	unsigned int maxVertices = MS_MESHLET_SIZE; // TODO: Separate config for max vertices and max primitives per meshlet
	unsigned int maxPrimitives = MS_MESHLET_SIZE;
	unsigned int minVertices = MS_MESHLET_MIN_SIZE;
	unsigned int minPrimitives = MS_MESHLET_MIN_SIZE;

	size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), maxVertices, minPrimitives);

	m_meshlets.resize(maxMeshlets);
	m_meshletVertices.resize(maxMeshlets * maxVertices);
	m_meshletTriangles.resize(maxMeshlets * maxPrimitives * 3);

    m_meshlets = std::vector<meshopt_Meshlet>(maxMeshlets);
	m_meshletVertices = std::vector<unsigned int>(maxMeshlets*maxVertices);
	m_meshletTriangles = std::vector<unsigned char>(maxMeshlets * maxPrimitives * 3);
    
	auto meshletCount = meshopt_buildMeshletsSpatial(
		m_meshlets.data(), 
		m_meshletVertices.data(), 
		m_meshletTriangles.data(), 
		indices.data(), 
		indices.size(), 
		(float*)m_vertices->data(), 
		m_vertices->size() / m_perMeshBufferData.vertexByteSize, 
		m_perMeshBufferData.vertexByteSize, 
		maxVertices, 
		minPrimitives,
		maxPrimitives, 
		0.5);

	m_meshlets.resize(meshletCount);

	size_t globalVertexCount =
		m_vertices->size() / m_perMeshBufferData.vertexByteSize;

	for (size_t i = 0; i < m_meshlets.size(); ++i) {
		auto& meshlet = m_meshlets[i];

		const unsigned int* verts = m_meshletVertices.data() + meshlet.vertex_offset;
		const unsigned char* tris = m_meshletTriangles.data() + meshlet.triangle_offset;

		meshopt_Bounds bounds = meshopt_computeMeshletBounds(
			verts,
			tris,
			meshlet.triangle_count,
			reinterpret_cast<const float*>(m_vertices->data()),
			globalVertexCount,
			m_perMeshBufferData.vertexByteSize
		);

		m_meshletBounds.push_back({ { bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius } });
	}

	m_perMeshBufferData.numMeshlets = static_cast<unsigned int>(m_meshlets.size());
}

void Mesh::ComputeAABB(DirectX::XMFLOAT3& min, DirectX::XMFLOAT3& max) {
	// Initialize min and max vectors
	min.x = min.y = min.z = std::numeric_limits<float>::infinity();
	max.x = max.y = max.z = -std::numeric_limits<float>::infinity();

	unsigned int positionsByteStride = m_perMeshBufferData.vertexByteSize;
	unsigned int positionsOffset = 0;
	for (unsigned int i = 0; i < m_vertices->size(); i += positionsByteStride) {
		XMFLOAT3* position = (XMFLOAT3*)(m_vertices->data() + i + positionsOffset);
		min.x = (std::min)(min.x, position->x);
		min.y = (std::min)(min.y, position->y);
		min.z = (std::min)(min.z, position->z);
		max.x = (std::max)(max.x, position->x);
		max.y = (std::max)(max.y, position->y);
		max.z = (std::max)(max.z, position->z);
	}
}

void Mesh::ComputeBoundingSphere(const std::vector<UINT32>& indices) {
	BoundingSphere sphere = {};

	XMFLOAT3 min, max;

	// Compute the Axis-Aligned Bounding Box
	ComputeAABB(min, max);
	XMFLOAT3 center = Scale(Add(min, max), 0.5f);

	// Compute the radius of the bounding sphere
	XMFLOAT3 diagonal = Subtract(max, min);
	float radius = 0.5f * sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y + diagonal.z * diagonal.z);

	// Set the bounding sphere
	sphere.sphere = DirectX::XMFLOAT4(center.x, center.y, center.z, radius);

	m_perMeshBufferData.boundingSphere = sphere;
}

void Mesh::CreateMeshletReorderedVertices() {
	const size_t vertexByteSize  = m_perMeshBufferData.vertexByteSize;

	size_t totalVerts = 0;
	for (auto& ml : m_meshlets) {
		totalVerts += ml.vertex_count;
	}

	m_meshletReorderedVertices.resize(totalVerts * vertexByteSize);
	m_meshletReorderedSkinningVertices.resize(totalVerts * m_skinningVertexSize);

	// Post-skinning vertices
	std::byte* dst = m_meshletReorderedVertices.data();
	for (const auto& ml : m_meshlets) {
		for (unsigned int i = 0; i < ml.vertex_count; ++i) {
			unsigned int globalIndex = 
				m_meshletVertices[ml.vertex_offset + i];

			std::byte* src = m_vertices->data()
				+ globalIndex * vertexByteSize;

			std::copy_n(src, vertexByteSize, dst);
			dst += vertexByteSize;
		}
	}

	// Skinning vertices, if we have them
	std::byte* dstSkinning = m_meshletReorderedSkinningVertices.data();
	if (m_skinningVertices) {
		for (const auto& ml : m_meshlets) {
			for (unsigned int i = 0; i < ml.vertex_count; ++i) {
				unsigned int globalIndex =
					m_meshletVertices[ml.vertex_offset + i];
				std::byte* src = m_skinningVertices->data()
					+ globalIndex * m_skinningVertexSize;
				std::copy_n(src, m_skinningVertexSize, dstSkinning);
				dstSkinning += m_skinningVertexSize;
			}
		}
	}
}

void Mesh::CreateBuffers(const std::vector<UINT32>& indices) {

	BuildClusterLOD(indices);
	CreateMeshlets(indices);
	CreateMeshletReorderedVertices();
    CreateVertexBuffer();
	ComputeBoundingSphere(indices);

    const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(UINT32));
    m_indexCount = static_cast<uint32_t>(indices.size());

	m_indexBufferHandle = Buffer::CreateShared(rhi::HeapType::DeviceLocal, indexBufferSize);

	BUFFER_UPLOAD(indices.data(), indexBufferSize, UploadManager::UploadTarget::FromShared(m_indexBufferHandle), 0);

    m_indexBufferView.buffer = m_indexBufferHandle->GetAPIResource().GetHandle();
    m_indexBufferView.format = rhi::Format::R32_UInt;
    m_indexBufferView.sizeBytes = indexBufferSize;
}

rhi::VertexBufferView Mesh::GetVertexBufferView() const {
    return m_vertexBufferView;
}

rhi::IndexBufferView Mesh::GetIndexBufferView() const {
    return m_indexBufferView;
}

UINT Mesh::GetIndexCount() const {
    return m_indexCount;
}

int Mesh::GetNextGlobalIndex() {
    return globalMeshCount.fetch_add(1, std::memory_order_relaxed);
}

uint64_t Mesh::GetGlobalID() const {
	return m_globalMeshID;
}

void Mesh::SetPreSkinningVertexBufferView(std::unique_ptr<BufferView> view) {
	m_preSkinningVertexBufferView = std::move(view);
	m_perMeshBufferData.vertexBufferOffset = static_cast<uint32_t>(m_preSkinningVertexBufferView->GetOffset()); // TODO: Vertex buffer pool instead of one buffer limited to uint32 max

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

void Mesh::SetMeshletOffsetsBufferView(std::unique_ptr<BufferView> view) {
	m_meshletBufferView = std::move(view);
	m_perMeshBufferData.meshletBufferOffset = static_cast<uint32_t>(m_meshletBufferView->GetOffset());

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}
void Mesh::SetMeshletVerticesBufferView(std::unique_ptr<BufferView> view) {
	m_meshletVerticesBufferView = std::move(view);
	m_perMeshBufferData.meshletVerticesBufferOffset = static_cast<uint32_t>(m_meshletVerticesBufferView->GetOffset());

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}
void Mesh::SetMeshletTrianglesBufferView(std::unique_ptr<BufferView> view) {
	m_meshletTrianglesBufferView = std::move(view);
	m_perMeshBufferData.meshletTrianglesBufferOffset = static_cast<uint32_t>(m_meshletTrianglesBufferView->GetOffset());

	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetBufferViews(std::unique_ptr<BufferView> preSkinningVertexBufferView, std::unique_ptr<BufferView> postSkinningVertexBufferView, std::unique_ptr<BufferView> meshletBufferView, std::unique_ptr<BufferView> meshletVerticesBufferView, std::unique_ptr<BufferView> meshletTrianglesBufferView, std::unique_ptr<BufferView> meshletBoundsBufferView) {
	m_postSkinningVertexBufferView = std::move(postSkinningVertexBufferView);
	m_preSkinningVertexBufferView = std::move(preSkinningVertexBufferView);
	m_meshletBufferView = std::move(meshletBufferView);
	m_meshletVerticesBufferView = std::move(meshletVerticesBufferView);
	m_meshletTrianglesBufferView = std::move(meshletTrianglesBufferView);
	m_meshletBoundsBufferView = std::move(meshletBoundsBufferView);
	if (m_meshletBufferView == nullptr || m_meshletVerticesBufferView == nullptr || m_meshletTrianglesBufferView == nullptr) {
		return; // We're probably deleting the mesh
	}
	if (m_preSkinningVertexBufferView != nullptr) { // If the mesh is skinned
		m_perMeshBufferData.vertexBufferOffset = static_cast<uint32_t>(m_preSkinningVertexBufferView->GetOffset()); // TODO: Vertex buffer pool instead of one buffer limited to uint32 max
	}
	else { // If the mesh is not skinned
		m_perMeshBufferData.vertexBufferOffset = static_cast<uint32_t>(m_postSkinningVertexBufferView->GetOffset()); // same
	}
	m_perMeshBufferData.meshletBufferOffset = static_cast<uint32_t>(m_meshletBufferView->GetOffset() / sizeof(meshopt_Meshlet));
	m_perMeshBufferData.meshletVerticesBufferOffset = static_cast<uint32_t>(m_meshletVerticesBufferView->GetOffset() / 4);
	m_perMeshBufferData.meshletTrianglesBufferOffset = static_cast<uint32_t>(m_meshletTrianglesBufferView->GetOffset());
	if (m_pCurrentMeshManager != nullptr && m_perMeshBufferView != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetCLodBufferViews(
	std::unique_ptr<BufferView> clusterLODGroupsView,
	std::unique_ptr<BufferView> clusterLODChildrenView,
	std::unique_ptr<BufferView> clusterLODMeshletsView,
	std::unique_ptr<BufferView> clusterLODMeshletBoundsView,
	std::unique_ptr<BufferView> childLocalMeshletIndicesView
) {
	m_clusterLODGroupsView = std::move(clusterLODGroupsView);
	m_clusterLODChildrenView = std::move(clusterLODChildrenView);
	m_clusterLODMeshletsView = std::move(clusterLODMeshletsView);
	m_clusterLODMeshletBoundsView = std::move(clusterLODMeshletBoundsView);
	m_childLocalMeshletIndicesView = std::move(childLocalMeshletIndicesView);
}

void Mesh::SetBaseSkin(std::shared_ptr<Skeleton> skeleton) {
	m_baseSkeleton = skeleton;
}

void Mesh::UpdateVertexCount(bool meshletReorderedVertices) {
	m_perMeshBufferData.numVertices = static_cast<uint32_t>(meshletReorderedVertices ? m_meshletReorderedVertices.size() / m_perMeshBufferData.vertexByteSize : m_vertices->size() / m_perMeshBufferData.vertexByteSize);
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}

void Mesh::SetMaterialDataIndex(unsigned int index) {
	m_perMeshBufferData.materialDataIndex = index;
	if (m_pCurrentMeshManager != nullptr) {
		m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
	}
}
