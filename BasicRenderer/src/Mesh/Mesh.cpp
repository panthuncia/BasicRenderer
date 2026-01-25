#include "Mesh/Mesh.h"

#include <limits>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <numeric>

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

namespace
{
	struct U32Stats
	{
		uint32_t count = 0;
		uint64_t sum = 0;
		uint32_t minv = std::numeric_limits<uint32_t>::max();
		uint32_t maxv = 0;

		void add(uint32_t v)
		{
			++count;
			sum += v;
			minv = std::min(minv, v);
			maxv = std::max(maxv, v);
		}

		double avg() const { return count ? double(sum) / double(count) : 0.0; }
		bool   any() const { return count != 0; }
	};
}


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

void Mesh::LogClusterLODHierarchyStats() const
{
	if (m_clodGroups.empty())
	{
		spdlog::info("ClusterLOD stats: no groups (hierarchy not built).");
		return;
	}

	const uint32_t maxDepth = m_clodMaxDepth;
	const uint32_t lodLevelCount = maxDepth + 1;

	// -------------------------
	// Group-level stats
	// -------------------------
	U32Stats groups_meshletsPerGroup;
	U32Stats groups_childBucketsPerGroup;

	std::vector<uint32_t> groupsPerDepth(lodLevelCount, 0);
	std::vector<uint64_t> meshletsPerDepth(lodLevelCount, 0);

	uint64_t totalMeshlets = 0;
	for (const ClusterLODGroup& g : m_clodGroups)
	{
		const uint32_t d = uint32_t(g.depth);
		if (d < lodLevelCount)
		{
			groupsPerDepth[d] += 1;
			meshletsPerDepth[d] += g.meshletCount;
		}

		groups_meshletsPerGroup.add(g.meshletCount);
		groups_childBucketsPerGroup.add(g.childCount);
		totalMeshlets += g.meshletCount;
	}

	// -------------------------
	// Child-bucket stats
	// -------------------------
	U32Stats child_localMeshletsPerBucket;
	uint32_t terminalBuckets = 0; // refinedGroup == -1
	uint32_t refinedBuckets = 0; // refinedGroup != -1

	for (const ClusterLODChild& c : m_clodChildren)
	{
		child_localMeshletsPerBucket.add(c.localMeshletCount);
		if (c.refinedGroup < 0) terminalBuckets++;
		else refinedBuckets++;
	}

	// -------------------------
	// Traversal-node stats
	// -------------------------
	const uint32_t nodeCount = uint32_t(m_clodNodes.size());

	uint32_t leafGroupNodes = 0;
	uint32_t interiorNodes = 0;
	U32Stats interior_childCount;

	for (uint32_t i = 0; i < nodeCount; ++i)
	{
		const ClusterLODNode& n = m_clodNodes[i];

		if (n.range.isGroup)
		{
			leafGroupNodes++;
		}
		else
		{
			interiorNodes++;
			const uint32_t childCount = uint32_t(n.range.countMinusOne) + 1u;
			interior_childCount.add(childCount);
		}
	}

	// -------------------------
	// Log header / overview
	// -------------------------
	spdlog::info("ClusterLOD stats:");
	spdlog::info("  groups={} | meshlets={} | depths={} (0..{})",
		uint32_t(m_clodGroups.size()),
		totalMeshlets,
		lodLevelCount,
		maxDepth);

	spdlog::info("  children: entries={} | localMeshletIndexCount={}",
		uint32_t(m_clodChildren.size()),
		uint32_t(m_clodChildLocalMeshletIndices.size()));

	spdlog::info("  traversal nodes: total={} | interior={} | leafGroupNodes={}",
		nodeCount, interiorNodes, leafGroupNodes);

	// -------------------------
	// Log distributions
	// -------------------------
	if (groups_meshletsPerGroup.any())
	{
		spdlog::info("  meshlets per group: min={} | avg={:.2f} | max={}",
			groups_meshletsPerGroup.minv, groups_meshletsPerGroup.avg(), groups_meshletsPerGroup.maxv);
	}

	if (groups_childBucketsPerGroup.any())
	{
		spdlog::info("  child buckets per group: min={} | avg={:.2f} | max={}",
			groups_childBucketsPerGroup.minv, groups_childBucketsPerGroup.avg(), groups_childBucketsPerGroup.maxv);
	}

	if (child_localMeshletsPerBucket.any())
	{
		spdlog::info("  local meshlets per child bucket: min={} | avg={:.2f} | max={}",
			child_localMeshletsPerBucket.minv, child_localMeshletsPerBucket.avg(), child_localMeshletsPerBucket.maxv);
	}

	spdlog::info("  child bucket types: terminal(refined=-1)={} | refined(refined>=0)={}",
		terminalBuckets, refinedBuckets);

	if (interior_childCount.any())
	{
		spdlog::info("  interior node child count: min={} | avg={:.2f} | max={}",
			interior_childCount.minv, interior_childCount.avg(), interior_childCount.maxv);
	}

	// -------------------------
	// Per-depth summary
	// -------------------------
	spdlog::info("  per-depth summary:");
	for (uint32_t d = 0; d < lodLevelCount; ++d)
	{
		const uint32_t depthRootIndex = 1 + d;

		// Nodes for this depth tree = 1 root + range.count (range excludes that root by your allocator)
		uint32_t depthNodes = 1;
		if (d < m_clodLodNodeRanges.size())
			depthNodes += m_clodLodNodeRanges[d].count;

		const bool depthRootIsLeaf = (depthRootIndex < nodeCount) ? (m_clodNodes[depthRootIndex].range.isGroup != 0) : false;

		spdlog::info("    depth {}: groups={} | meshlets={} | traversalNodes={} | depthRootIsLeaf={}",
			d,
			groupsPerDepth[d],
			(uint64_t)meshletsPerDepth[d],
			depthNodes,
			depthRootIsLeaf);
	}

	// Top root info (node 0)
	if (nodeCount > 0)
	{
		const ClusterLODNode& top = m_clodNodes[0];
		const uint32_t topChildren = top.range.isGroup ? 0u : (uint32_t(top.range.countMinusOne) + 1u);
		spdlog::info("  top root: node={} | childCount={} | childOffset={}",
			m_clodTopRootNode,
			topChildren,
			uint32_t(top.range.indexOrOffset));
	}
}

void Mesh::BuildClusterLODTraversalHierarchy(uint32_t preferredNodeWidth)
{
	if (m_clodGroups.empty())
		return;

	preferredNodeWidth = std::max(2u, preferredNodeWidth);

	// Gather groups by depth + compute max depth
	m_clodMaxDepth = 0;
	for (const ClusterLODGroup& g : m_clodGroups)
		m_clodMaxDepth = std::max(m_clodMaxDepth, uint32_t(g.depth));

	const uint32_t lodLevelCount = m_clodMaxDepth + 1;

	std::vector<std::vector<uint32_t>> groupsByDepth(lodLevelCount);
	groupsByDepth.shrink_to_fit();
	for (uint32_t groupID = 0; groupID < uint32_t(m_clodGroups.size()); ++groupID)
	{
		const uint32_t d = uint32_t(m_clodGroups[groupID].depth);
		groupsByDepth[d].push_back(groupID);
	}

	// Sanity: every depth should exist
	for (uint32_t d = 0; d < lodLevelCount; ++d)
	{
		if (groupsByDepth[d].empty())
		{
			throw std::runtime_error("Cluster LOD: missing groups for an intermediate depth; compact depths or handle gaps.");
		}
	}

	// Allocate node layout like NVIDIA:
	// [0] top root
	// [1..lodLevelCount] per-depth roots
	// [rest] per-depth interior nodes + leaves (except per-depth root)
	m_clodLodNodeRanges.assign(lodLevelCount, {});
	m_clodLodLevelRoots.resize(lodLevelCount);
	for (uint32_t d = 0; d < lodLevelCount; ++d)
		m_clodLodLevelRoots[d] = 1 + d;

	uint32_t nodeOffset = 1 + lodLevelCount;

	for (uint32_t depth = 0; depth < lodLevelCount; ++depth)
	{
		const uint32_t leafCount = uint32_t(groupsByDepth[depth].size());

		// leaves + all interior layers including root
		uint32_t nodeCount = leafCount;
		uint32_t iterCount = leafCount;

		while (iterCount > 1)
		{
			iterCount = (iterCount + preferredNodeWidth - 1) / preferredNodeWidth;
			nodeCount += iterCount;
		}

		// subtract root (stored at index 1+depth)
		nodeCount--;

		m_clodLodNodeRanges[depth].offset = nodeOffset;
		m_clodLodNodeRanges[depth].count = nodeCount;
		nodeOffset += nodeCount;
	}

	m_clodNodes.clear();
	m_clodNodes.resize(nodeOffset);

	// Build per-depth trees
	for (uint32_t depth = 0; depth < lodLevelCount; ++depth)
	{
		const auto& groupIDs = groupsByDepth[depth];
		const uint32_t leafCount = uint32_t(groupIDs.size());
		const ClusterLODNodeRangeAlloc& range = m_clodLodNodeRanges[depth];

		uint32_t writeOffset = range.offset;
		uint32_t lastLayerOffset = writeOffset;

		// Leaves (groups)
		for (uint32_t i = 0; i < leafCount; ++i)
		{
			const uint32_t groupID = groupIDs[i];
			const ClusterLODGroup& grp = m_clodGroups[groupID];

			ClusterLODNode& node = (leafCount == 1) ? m_clodNodes[1 + depth] : m_clodNodes[writeOffset++];

			node = {};
			node.range.isGroup = 1;
			node.range.indexOrOffset = groupID;
			node.range.countMinusOne = (grp.meshletCount > 0) ? (grp.meshletCount - 1) : 0;

			node.traversalMetric.boundingSphereX = grp.bounds.center[0];
			node.traversalMetric.boundingSphereY = grp.bounds.center[1];
			node.traversalMetric.boundingSphereZ = grp.bounds.center[2];
			node.traversalMetric.boundingSphereRadius = grp.bounds.radius;
			node.traversalMetric.maxQuadricError = grp.bounds.error;
		}

		// Special case: single leaf stored into root section.
		if (leafCount == 1) {
			writeOffset++;
		}

		// Interior layers
		uint32_t iterCount = leafCount;

		std::vector<uint32_t> partitioned;
		std::vector<ClusterLODNode> scratch;

		while (iterCount > 1)
		{
			const uint32_t lastCount = iterCount;
			ClusterLODNode* lastNodes = &m_clodNodes[lastLayerOffset];

			// Partition children into spatially coherent buckets of preferredNodeWidth
			partitioned.resize(lastCount);
			meshopt_spatialClusterPoints(
				partitioned.data(),
				&lastNodes->traversalMetric.boundingSphereX,
				lastCount,
				sizeof(ClusterLODNode),
				preferredNodeWidth);

			// Reorder last layer by partition result
			scratch.assign(lastNodes, lastNodes + lastCount);
			for (uint32_t n = 0; n < lastCount; ++n)
				lastNodes[n] = scratch[partitioned[n]];

			// Build next layer
			iterCount = (lastCount + preferredNodeWidth - 1) / preferredNodeWidth;

			ClusterLODNode* newNodes = (iterCount == 1) ? &m_clodNodes[1 + depth] : &m_clodNodes[writeOffset];

			for (uint32_t n = 0; n < iterCount; ++n)
			{
				ClusterLODNode& node = newNodes[n];

				const uint32_t childBegin = n * preferredNodeWidth;
				const uint32_t childEnd = std::min(childBegin + preferredNodeWidth, lastCount);
				const uint32_t childCount = childEnd - childBegin;

				ClusterLODNode* children = &lastNodes[childBegin];

				node = {};
				node.range.isGroup = 0;
				node.range.indexOrOffset = lastLayerOffset + childBegin; // childOffset
				node.range.countMinusOne = childCount - 1;

				// max error
				float maxErr = 0.f;
				for (uint32_t c = 0; c < childCount; ++c)
					maxErr = std::max(maxErr, children[c].traversalMetric.maxQuadricError);
				node.traversalMetric.maxQuadricError = maxErr;

				// merged sphere
				meshopt_Bounds merged = meshopt_computeSphereBounds(
					&children[0].traversalMetric.boundingSphereX,
					childCount,
					sizeof(ClusterLODNode),
					&children[0].traversalMetric.boundingSphereRadius,
					sizeof(ClusterLODNode));

				node.traversalMetric.boundingSphereX = merged.center[0];
				node.traversalMetric.boundingSphereY = merged.center[1];
				node.traversalMetric.boundingSphereZ = merged.center[2];
				node.traversalMetric.boundingSphereRadius = merged.radius;
			}

			lastLayerOffset = writeOffset;
			writeOffset += iterCount;
		}

		// Match NVIDIA’s bookkeeping: writeOffset ends one past, then they -- and assert
		writeOffset--;
		if (range.offset + range.count != writeOffset)
			throw std::runtime_error("Cluster LOD: traversal node allocation mismatch (range/count).");
	}

	// Top root over per-depth roots [1..lodLevelCount]
	{
		meshopt_Bounds merged = meshopt_computeSphereBounds(
			&m_clodNodes[1].traversalMetric.boundingSphereX,
			lodLevelCount,
			sizeof(ClusterLODNode),
			&m_clodNodes[1].traversalMetric.boundingSphereRadius,
			sizeof(ClusterLODNode));

		ClusterLODNode& root = m_clodNodes[0];
		root = {};
		root.range.isGroup = 0;
		root.range.indexOrOffset = 1;                 // childOffset
		root.range.countMinusOne = lodLevelCount - 1; // childCountMinusOne

		root.traversalMetric.boundingSphereX = merged.center[0];
		root.traversalMetric.boundingSphereY = merged.center[1];
		root.traversalMetric.boundingSphereZ = merged.center[2];
		root.traversalMetric.boundingSphereRadius = merged.radius;

		float maxErr = 0.f;
		for (uint32_t c = 0; c < lodLevelCount; ++c)
			maxErr = std::max(maxErr, m_clodNodes[1 + c].traversalMetric.maxQuadricError);
		root.traversalMetric.maxQuadricError = maxErr;

		m_clodTopRootNode = 0;
	}
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
	m_clodChildren.clear();
	m_clodChildLocalMeshletIndices.clear();

	// traversal hierarchy storage
	m_clodNodes.clear();
	m_clodLodNodeRanges.clear();
	m_clodLodLevelRoots.clear();
	m_clodTopRootNode = 0;
	m_clodMaxDepth = 0;

	static_assert(sizeof(UINT32) == sizeof(unsigned int), "UINT32 must be 32-bit");
	const unsigned int* idx = reinterpret_cast<const unsigned int*>(indices.data());

	const size_t vertexStrideBytes = m_perMeshBufferData.vertexByteSize;
	const size_t globalVertexCount = m_vertices->size() / vertexStrideBytes;

	clodMesh mesh{};
	mesh.indices = idx;
	mesh.index_count = indices.size();
	mesh.vertex_count = globalVertexCount;

	mesh.vertex_positions = reinterpret_cast<const float*>(m_vertices->data());
	mesh.vertex_positions_stride = vertexStrideBytes;

	// positions-only simplification for now
	mesh.vertex_attributes = nullptr;
	mesh.vertex_attributes_stride = 0;
	mesh.vertex_lock = nullptr;
	mesh.attribute_weights = nullptr;
	mesh.attribute_count = 0;
	mesh.attribute_protect_mask = 0;

	clodConfig config = clodDefaultConfig(/*max_triangles=*/MS_MESHLET_SIZE);
	config.max_vertices = MS_MESHLET_SIZE;
	config.max_triangles = MS_MESHLET_SIZE;
	config.min_triangles = MS_MESHLET_MIN_SIZE;

	config.cluster_spatial = true;
	config.cluster_fill_weight = 0.5f;
	config.partition_spatial = true;
	config.partition_sort = true;
	config.optimize_clusters = true;
	config.optimize_bounds = true;

	// If you want <= MaxChildren refined groups per parent, account for meshopt partition overshoot (~+1/3).
	const uint32_t MaxChildren = 32;
	config.partition_size = std::max<size_t>(1, (MaxChildren * 3) / 4);

	uint32_t maxChildrenObserved = 0;

	clodBuild(config, mesh,
		[&](clodGroup group, const clodCluster* clusters, size_t cluster_count) -> int
		{
			m_clodMaxDepth = (std::max)(m_clodMaxDepth, uint32_t(group.depth));

			const uint32_t firstMeshlet = uint32_t(m_clodMeshlets.size());
			const uint32_t meshletCount = uint32_t(cluster_count);
			const int32_t groupId = int32_t(m_clodGroups.size());

			ClusterLODGroup outGroup{};
			outGroup.bounds = group.simplified;
			outGroup.firstMeshlet = firstMeshlet;
			outGroup.meshletCount = meshletCount;
			outGroup.depth = group.depth;
			outGroup.firstChild = 0;
			outGroup.childCount = 0;

			m_clodGroups.push_back(outGroup);

			// Child buckets keyed by refined group id
			struct ChildBucket
			{
				int32_t refinedGroup = -1;
				std::vector<uint32_t> locals; // meshlet indices local to this group
			};

			std::vector<ChildBucket> buckets;
			buckets.reserve(cluster_count);

			std::unordered_map<int32_t, uint32_t> bucketLookup;
			bucketLookup.reserve(cluster_count);

			auto add_to_bucket = [&](int32_t refined, uint32_t localIndex)
				{
					auto it = bucketLookup.find(refined);
					if (it != bucketLookup.end())
					{
						buckets[it->second].locals.push_back(localIndex);
						return;
					}
					const uint32_t newIdx = uint32_t(buckets.size());
					bucketLookup[refined] = newIdx;
					buckets.push_back(ChildBucket{ refined, {} });
					buckets.back().locals.reserve(8);
					buckets.back().locals.push_back(localIndex);
				};

			for (size_t i = 0; i < cluster_count; ++i)
			{
				const clodCluster& c = clusters[i];
				const uint32_t triCount = uint32_t(c.index_count / 3);

				add_to_bucket(int32_t(c.refined), uint32_t(i));

				// Convert to meshlet-local indices
				std::vector<unsigned int> localVerts(c.vertex_count);
				std::vector<unsigned char> localTris(c.index_count);

				const size_t unique = clodLocalIndices(
					localVerts.data(),
					localTris.data(),
					c.indices,
					c.index_count);

				// clodLocalIndices should match cluster vertex_count
				assert(unique == c.vertex_count);

				meshopt_Meshlet ml{};
				ml.vertex_offset = uint32_t(m_clodMeshletVertices.size());
				ml.triangle_offset = uint32_t(m_clodMeshletTriangles.size());
				ml.vertex_count = uint32_t(unique);
				ml.triangle_count = triCount;

				m_clodMeshletVertices.insert(m_clodMeshletVertices.end(), localVerts.begin(), localVerts.end());
				m_clodMeshletTriangles.insert(m_clodMeshletTriangles.end(), localTris.begin(), localTris.end());

				// Bounds: you can also use c.bounds directly; keeping your computeMeshletBounds path is fine.
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
				m_clodMeshletRefinedGroup.push_back(int32_t(c.refined));
			}

			// finalize child table for this group
			ClusterLODGroup& grp = m_clodGroups.back();
			grp.firstChild = uint32_t(m_clodChildren.size());
			grp.childCount = uint32_t(buckets.size());

			maxChildrenObserved = std::max(maxChildrenObserved, grp.childCount);

			for (const auto& b : buckets)
			{
				ClusterLODChild child{};
				child.refinedGroup = b.refinedGroup;
				child.firstLocalMeshletIndex = uint32_t(m_clodChildLocalMeshletIndices.size());
				child.localMeshletCount = uint32_t(b.locals.size());

				m_clodChildren.push_back(child);

				m_clodChildLocalMeshletIndices.insert(
					m_clodChildLocalMeshletIndices.end(),
					b.locals.begin(),
					b.locals.end());
			}

			return groupId;
		});

	if (maxChildrenObserved > MaxChildren)
		throw std::runtime_error("Exceeded maximum allowed Cluster LOD children per group");

	// build the acceleration hierarchy (BVH/K-ary trees per depth + top root)
	// Choose preferredNodeWidth independently; 8/16/32 are typical.
	BuildClusterLODTraversalHierarchy(/*preferredNodeWidth=*/MaxChildren);
	LogClusterLODHierarchyStats();
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
	std::unique_ptr<BufferView> childLocalMeshletIndicesView,
	std::unique_ptr<BufferView> clusterLODNodesView
) {
	m_clusterLODGroupsView = std::move(clusterLODGroupsView);
	m_clusterLODChildrenView = std::move(clusterLODChildrenView);
	m_clusterLODMeshletsView = std::move(clusterLODMeshletsView);
	m_clusterLODMeshletBoundsView = std::move(clusterLODMeshletBoundsView);
	m_childLocalMeshletIndicesView = std::move(childLocalMeshletIndicesView);
	m_clusterLODNodesView = std::move(clusterLODNodesView);
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
