#include "Mesh/Mesh.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <limits>

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

// -----------------------
// MeshSharedGeometry
// -----------------------

MeshSharedGeometry::~MeshSharedGeometry()
{
    // Delete the shared VB only once (when last reference to this geometry dies).
    auto& deletionManager = DeletionManager::GetInstance();
    deletionManager.MarkForDelete(vertexBufferHandle);
}

void MeshSharedGeometry::EnsureVertexBufferCreated()
{
    if (vertexBufferHandle) return;
    if (!vertices || vertices->empty() || vertexByteSize == 0) return;

    const UINT vertexBufferSize = static_cast<UINT>(vertices->size());
    vertexBufferHandle = ResourceManager::GetInstance().CreateBuffer(vertexBufferSize, (void*)vertices->data());

    vertexBufferView.buffer = vertexBufferHandle->GetAPIResource().GetHandle();
    vertexBufferView.stride = vertexByteSize; // FIX: don't use sizeof(vertexByteSize)
    vertexBufferView.sizeBytes = vertexBufferSize;
}

// -----------------------
// Mesh
// -----------------------

Mesh::Mesh(
    std::shared_ptr<MeshSharedGeometry> geom,
    const std::vector<UINT32>& indices,
    const std::shared_ptr<Material> material,
    unsigned int flags,
    bool createGpuBuffers)
{
    m_geom = std::move(geom);
    assert(m_geom && m_geom->vertices && "Mesh requires shared geometry with vertex data");

    m_cpuIndices = indices;

    m_perMeshBufferData.vertexFlags = flags;
    m_perMeshBufferData.vertexByteSize = m_geom->vertexByteSize;
    m_perMeshBufferData.numVertices = static_cast<uint32_t>(m_geom->GetNumVertices());
    m_perMeshBufferData.skinningVertexByteSize = m_geom->skinningVertexByteSize;

    m_skinningVertexSize = m_geom->skinningVertexByteSize;

    // CPU-derived data is always built (meshlets, bounds, reordered CPU streams)
    CreateMeshlets(m_cpuIndices);
    CreateMeshletReorderedVertices();
    ComputeBoundingSphere(m_cpuIndices);

    // Optional GPU buffer creation (VB shared, IB per Mesh)
    if (createGpuBuffers) {
        EnsureGpuBuffersCreated();
    }

    this->material = material;
    m_globalMeshID = GetNextGlobalIndex();
}

void Mesh::EnsureGpuBuffersCreated()
{
    if (m_gpuBuffersCreated) return;

    if (m_geom) {
        m_geom->EnsureVertexBufferCreated();
    }
    EnsureIndexBufferCreated();

    m_gpuBuffersCreated = true;
}

void Mesh::EnsureIndexBufferCreated()
{
    if (m_indexBufferHandle) return;
    if (m_cpuIndices.empty()) return;

    const UINT indexBufferSize = static_cast<UINT>(m_cpuIndices.size() * sizeof(UINT32));
    m_indexCount = static_cast<uint32_t>(m_cpuIndices.size());

    m_indexBufferHandle = ResourceManager::GetInstance().CreateBuffer(indexBufferSize, (void*)m_cpuIndices.data());

    m_indexBufferView.buffer = m_indexBufferHandle->GetAPIResource().GetHandle();
    m_indexBufferView.format = rhi::Format::R32_UInt;
    m_indexBufferView.sizeBytes = indexBufferSize;
}

void Mesh::CreateMeshlets(const std::vector<UINT32>& indices)
{
    unsigned int maxVertices = MS_MESHLET_SIZE;     // TODO: Separate config for max vertices and max primitives per meshlet
    unsigned int maxPrimitives = MS_MESHLET_SIZE;
    unsigned int minVertices = MS_MESHLET_MIN_SIZE;
    unsigned int minPrimitives = MS_MESHLET_MIN_SIZE;

    size_t maxMeshlets = meshopt_buildMeshletsBound(indices.size(), maxVertices, minPrimitives);

    m_meshlets.resize(maxMeshlets);
    m_meshletVertices.resize(maxMeshlets * maxVertices);
    m_meshletTriangles.resize(maxMeshlets * maxPrimitives * 3);

    auto meshletCount = meshopt_buildMeshletsSpatial(
        m_meshlets.data(),
        m_meshletVertices.data(),
        m_meshletTriangles.data(),
        indices.data(),
        indices.size(),
        (float*)m_geom->vertices->data(),
        m_geom->vertices->size() / m_perMeshBufferData.vertexByteSize,
        m_perMeshBufferData.vertexByteSize,
        maxVertices,
        minPrimitives,
        maxPrimitives,
        0.5);

    m_meshlets.resize(meshletCount);

    const size_t globalVertexCount = m_geom->vertices->size() / m_perMeshBufferData.vertexByteSize;

    m_meshletBounds.clear();
    m_meshletBounds.reserve(m_meshlets.size());

    for (size_t i = 0; i < m_meshlets.size(); ++i) {
        auto& meshlet = m_meshlets[i];

        const unsigned int* verts = m_meshletVertices.data() + meshlet.vertex_offset;
        const unsigned char* tris = m_meshletTriangles.data() + meshlet.triangle_offset;

        meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            verts,
            tris,
            meshlet.triangle_count,
            reinterpret_cast<const float*>(m_geom->vertices->data()),
            globalVertexCount,
            m_perMeshBufferData.vertexByteSize);

        m_meshletBounds.push_back({ { bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius } });
    }

    m_perMeshBufferData.numMeshlets = static_cast<unsigned int>(m_meshlets.size());
}

void Mesh::ComputeAABB(DirectX::XMFLOAT3& min, DirectX::XMFLOAT3& max)
{
    min.x = min.y = min.z = std::numeric_limits<float>::infinity();
    max.x = max.y = max.z = -std::numeric_limits<float>::infinity();

    const unsigned int stride = m_perMeshBufferData.vertexByteSize;
    const unsigned int posOffset = 0;

    for (unsigned int i = 0; i < m_geom->vertices->size(); i += stride) {
        auto* position = (DirectX::XMFLOAT3*)(m_geom->vertices->data() + i + posOffset);
        min.x = (std::min)(min.x, position->x);
        min.y = (std::min)(min.y, position->y);
        min.z = (std::min)(min.z, position->z);
        max.x = (std::max)(max.x, position->x);
        max.y = (std::max)(max.y, position->y);
        max.z = (std::max)(max.z, position->z);
    }
}

void Mesh::ComputeBoundingSphere(const std::vector<UINT32>& /*indices*/)
{
    BoundingSphere sphere = {};

    DirectX::XMFLOAT3 min, max;
    ComputeAABB(min, max);
    DirectX::XMFLOAT3 center = Scale(Add(min, max), 0.5f);

    DirectX::XMFLOAT3 diagonal = Subtract(max, min);
    float radius = 0.5f * std::sqrt(diagonal.x * diagonal.x + diagonal.y * diagonal.y + diagonal.z * diagonal.z);

    sphere.sphere = DirectX::XMFLOAT4(center.x, center.y, center.z, radius);
    m_perMeshBufferData.boundingSphere = sphere;
}

void Mesh::CreateMeshletReorderedVertices()
{
    const size_t vertexByteSize = m_perMeshBufferData.vertexByteSize;

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
            unsigned int globalIndex = m_meshletVertices[ml.vertex_offset + i];

            std::byte* src = m_geom->vertices->data()
                + globalIndex * vertexByteSize;

            std::copy_n(src, vertexByteSize, dst);
            dst += vertexByteSize;
        }
    }

    // Skinning vertices, if we have them
    if (m_geom->skinningVertices) {
        std::byte* dstSkinning = m_meshletReorderedSkinningVertices.data();
        for (const auto& ml : m_meshlets) {
            for (unsigned int i = 0; i < ml.vertex_count; ++i) {
                unsigned int globalIndex = m_meshletVertices[ml.vertex_offset + i];
                std::byte* src = m_geom->skinningVertices->data()
                    + globalIndex * m_skinningVertexSize;
                std::copy_n(src, m_skinningVertexSize, dstSkinning);
                dstSkinning += m_skinningVertexSize;
            }
        }
    }
}

rhi::VertexBufferView Mesh::GetVertexBufferView() const
{
    return m_geom ? m_geom->vertexBufferView : rhi::VertexBufferView{};
}

rhi::IndexBufferView Mesh::GetIndexBufferView() const
{
    return m_indexBufferView;
}

UINT Mesh::GetIndexCount() const
{
    return m_indexCount;
}

int Mesh::GetNextGlobalIndex()
{
    return globalMeshCount.fetch_add(1, std::memory_order_relaxed);
}

uint64_t Mesh::GetGlobalID() const
{
    return m_globalMeshID;
}

void Mesh::SetPreSkinningVertexBufferView(std::unique_ptr<BufferView> view)
{
    m_preSkinningVertexBufferView = std::move(view);
    m_perMeshBufferData.vertexBufferOffset = static_cast<uint32_t>(m_preSkinningVertexBufferView->GetOffset()); // TODO: Vertex buffer pool instead of one buffer limited to uint32 max

    if (m_pCurrentMeshManager != nullptr) {
        m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
    }
}

BufferView* Mesh::GetPreSkinningVertexBufferView()
{
    return m_preSkinningVertexBufferView.get();
}

BufferView* Mesh::GetPostSkinningVertexBufferView()
{
    return m_postSkinningVertexBufferView.get();
}

void Mesh::SetMeshletOffsetsBufferView(std::unique_ptr<BufferView> view)
{
    m_meshletBufferView = std::move(view);
    m_perMeshBufferData.meshletBufferOffset = static_cast<uint32_t>(m_meshletBufferView->GetOffset());

    if (m_pCurrentMeshManager != nullptr) {
        m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
    }
}
void Mesh::SetMeshletVerticesBufferView(std::unique_ptr<BufferView> view)
{
    m_meshletVerticesBufferView = std::move(view);
    m_perMeshBufferData.meshletVerticesBufferOffset = static_cast<uint32_t>(m_meshletVerticesBufferView->GetOffset());

    if (m_pCurrentMeshManager != nullptr) {
        m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
    }
}
void Mesh::SetMeshletTrianglesBufferView(std::unique_ptr<BufferView> view)
{
    m_meshletTrianglesBufferView = std::move(view);
    m_perMeshBufferData.meshletTrianglesBufferOffset = static_cast<uint32_t>(m_meshletTrianglesBufferView->GetOffset());

    if (m_pCurrentMeshManager != nullptr) {
        m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
    }
}

void Mesh::SetBufferViews(std::unique_ptr<BufferView> preSkinningVertexBufferView,
    std::unique_ptr<BufferView> postSkinningVertexBufferView,
    std::unique_ptr<BufferView> meshletBufferView,
    std::unique_ptr<BufferView> meshletVerticesBufferView,
    std::unique_ptr<BufferView> meshletTrianglesBufferView,
    std::unique_ptr<BufferView> meshletBoundsBufferView)
{
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

void Mesh::SetBaseSkin(std::shared_ptr<Skeleton> skeleton)
{
    m_baseSkeleton = skeleton;
    //m_perMeshBufferData.boneTransformBufferIndex = skeleton->GetTransformsBufferIndex();
    m_perMeshBufferData.inverseBindMatricesBufferIndex = skeleton->GetInverseBindMatricesBufferIndex();
    if (m_pCurrentMeshManager != nullptr) {
        m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
    }
    //skeleton->userIDs.push_back(localID);
}

void Mesh::UpdateVertexCount(bool meshletReorderedVertices)
{
    const uint64_t vcount = meshletReorderedVertices
        ? (m_meshletReorderedVertices.size() / m_perMeshBufferData.vertexByteSize)
        : (m_geom->vertices->size() / m_perMeshBufferData.vertexByteSize);

    m_perMeshBufferData.numVertices = static_cast<uint32_t>(vcount);
    if (m_pCurrentMeshManager != nullptr) {
        m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
    }
}

void Mesh::SetMaterialDataIndex(unsigned int index)
{
    m_perMeshBufferData.materialDataIndex = index;
    if (m_pCurrentMeshManager != nullptr) {
        m_pCurrentMeshManager->UpdatePerMeshBuffer(m_perMeshBufferView, m_perMeshBufferData);
    }
}
