// MeshIngestBuilder out-of-line methods that have no GPU dependencies.
// The GPU-dependent Build() method is in Mesh.cpp.

#include "Mesh/ClusterLODTypes.h"
#include "Mesh/ClusterLODUtilities.h"

ClusterLODPrebuildArtifacts MeshIngestBuilder::BuildClusterLODArtifacts() const {
	const std::vector<std::byte>* skinningVertices = m_skinningVertices.empty() ? nullptr : &m_skinningVertices;
	return BuildClusterLODArtifactsFromGeometry(
		m_vertices,
		m_vertexSize,
		skinningVertices,
		m_skinningVertexSize,
		m_indices,
		m_flags,
		m_clusterLODBuilderSettings);
}
