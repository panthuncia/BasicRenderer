#pragma once

#include <directxmath.h>

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <atomic>
#include <optional>
#include <cassert>

#include <rhi.h>

#include "Mesh/VertexFlags.h"
#include "Import/MeshData.h"
#include "ShaderBuffers.h"
#include "meshoptimizer.h"
#include "Resources/Buffers/BufferView.h"
#include "Managers/Singletons/DeletionManager.h"

class Material;
class MeshManager;
class Skeleton;
class Buffer;

// Shared across multiple material subsets that reference the same vertex data.
// Owns CPU vertex blobs and (optionally) the GPU vertex buffer.
struct MeshSharedGeometry
{
    std::shared_ptr<std::vector<std::byte>> vertices;
    std::shared_ptr<std::vector<std::byte>> skinningVertices; // null if not skinned

    uint32_t vertexByteSize = 0;
    uint32_t skinningVertexByteSize = 0;
    uint32_t vertexFlags = 0;

    // Optional GPU backing (created once, shared)
    std::shared_ptr<Buffer> vertexBufferHandle;
    rhi::VertexBufferView vertexBufferView{};

    MeshSharedGeometry() = default;
    MeshSharedGeometry(const MeshSharedGeometry&) = delete;
    MeshSharedGeometry& operator=(const MeshSharedGeometry&) = delete;

    ~MeshSharedGeometry();

    uint64_t GetNumVertices() const
    {
        if (!vertices || vertexByteSize == 0) return 0;
        return static_cast<uint64_t>(vertices->size() / vertexByteSize);
    }

    bool HasSkinning() const { return (bool)skinningVertices && skinningVertexByteSize != 0; }
	uint32_t GetVertexFlags() const { return vertexFlags; }

    // Creates the GPU vertex buffer once (no-op if already created).
    void EnsureVertexBufferCreated();
};

class Mesh {
public:
    ~Mesh()
    {
        // Vertex buffer lifetime is owned by MeshSharedGeometry (shared across subsets).
        auto& deletionManager = DeletionManager::GetInstance();
        deletionManager.MarkForDelete(m_indexBufferHandle);
    }

    // Existing entry point (used by MeshFromData). Default behavior keeps creating GPU buffers.
    static std::shared_ptr<Mesh> CreateShared(
        std::unique_ptr<std::vector<std::byte>> vertices,
        unsigned int vertexSize,
        std::optional<std::unique_ptr<std::vector<std::byte>>> skinningVertices,
        unsigned int skinningVertexSize,
        const std::vector<UINT32>& indices,
        const std::shared_ptr<Material> material,
        unsigned int flags,
        bool createGpuBuffers = true)
    {
        // Convert unique ownership into shared ownership (refcount starts at 1).
        auto geom = std::make_shared<MeshSharedGeometry>();
        geom->vertices = std::shared_ptr<std::vector<std::byte>>(vertices.release());
        if (skinningVertices.has_value()) {
            geom->skinningVertices = std::shared_ptr<std::vector<std::byte>>(skinningVertices.value().release());
        }
        geom->vertexByteSize = vertexSize;
        geom->skinningVertexByteSize = skinningVertexSize;
        return std::shared_ptr<Mesh>(new Mesh(std::move(geom), indices, material, flags, createGpuBuffers));
    }

    // Create a Mesh that shares vertex data with other Meshes (per-material subset meshes).
    // Default is CPU-only; call EnsureGpuBuffersCreated later
    static std::shared_ptr<Mesh> CreateFromSharedGeometry(
        std::shared_ptr<MeshSharedGeometry> geometry,
        const std::vector<UINT32>& indices,
        const std::shared_ptr<Material> material,
        bool createGpuBuffers = false)
    {
        return std::shared_ptr<Mesh>(new Mesh(std::move(geometry), indices, material, geometry->GetVertexFlags(), createGpuBuffers));
    }

    uint64_t GetNumVertices(bool meshletReorderedVertices) const
    {
        const uint64_t base = m_geom ? m_geom->GetNumVertices() : 0ull;
        return meshletReorderedVertices
            ? static_cast<uint64_t>(m_meshletReorderedVertices.size() / m_perMeshBufferData.vertexByteSize)
            : base;
    }

    rhi::VertexBufferView GetVertexBufferView() const;
    rhi::IndexBufferView GetIndexBufferView() const;
    PerMeshCB& GetPerMeshCBData() { return m_perMeshBufferData; };
    UINT GetIndexCount() const;
    uint64_t GetGlobalID() const;

    std::vector<std::byte>& GetVertices() { assert(m_geom && m_geom->vertices); return *m_geom->vertices; }
    std::vector<std::byte>& GetMeshletReorderedVertices() { return m_meshletReorderedVertices; }
    std::vector<std::byte>& GetSkinningVertices() { assert(m_geom && m_geom->skinningVertices); return *m_geom->skinningVertices; }
    std::vector<std::byte>& GetMeshletReorderedSkinningVertices() { return m_meshletReorderedSkinningVertices; }
    std::vector<meshopt_Meshlet>& GetMeshlets() { return m_meshlets; }
    std::vector<unsigned int>& GetMeshletVertices() { return m_meshletVertices; }
    std::vector<unsigned char>& GetMeshletTriangles() { return m_meshletTriangles; }

    std::shared_ptr<Material> material;

    void SetPreSkinningVertexBufferView(std::unique_ptr<BufferView> view);
    BufferView* GetPreSkinningVertexBufferView();
    BufferView* GetPostSkinningVertexBufferView();
    void SetMeshletOffsetsBufferView(std::unique_ptr<BufferView> view);
    void SetMeshletVerticesBufferView(std::unique_ptr<BufferView> view);
    void SetMeshletTrianglesBufferView(std::unique_ptr<BufferView> view);

    BufferView* GetMeshletOffsetsBufferView() { return m_meshletBufferView.get(); }
    BufferView* GetMeshletVerticesBufferView() { return m_meshletVerticesBufferView.get(); }
    BufferView* GetMeshletTrianglesBufferView() { return m_meshletTrianglesBufferView.get(); }

    void SetBufferViews(std::unique_ptr<BufferView> preSkinningVertexBufferView,
        std::unique_ptr<BufferView> postSkinningVertexBufferView,
        std::unique_ptr<BufferView> meshletBufferView,
        std::unique_ptr<BufferView> meshletVerticesBufferView,
        std::unique_ptr<BufferView> meshletTrianglesBufferView,
        std::unique_ptr<BufferView> meshletBoundsBufferView);

    void SetBaseSkin(std::shared_ptr<Skeleton> skeleton);
    bool HasBaseSkin() const { return m_baseSkeleton != nullptr; }
    std::shared_ptr<Skeleton> GetBaseSkin() const { return m_baseSkeleton; }

    uint64_t GetMeshletBufferOffset() const { return m_meshletBufferView->GetOffset(); }
    uint64_t GetMeshletVerticesBufferOffset() const { return m_meshletVerticesBufferView->GetOffset(); }
    uint64_t GetMeshletTrianglesBufferOffset() const { return m_meshletTrianglesBufferView->GetOffset(); }

    uint32_t GetMeshletCount() { return static_cast<uint32_t>(m_meshlets.size()); }

    void SetPerMeshBufferView(std::unique_ptr<BufferView> view) { m_perMeshBufferView = std::move(view); }
    std::unique_ptr<BufferView>& GetPerMeshBufferView() { return m_perMeshBufferView; }

    void SetCurrentMeshManager(MeshManager* manager) { m_pCurrentMeshManager = manager; }

    unsigned int GetSkinningVertexSize() const { return m_skinningVertexSize; }

    void UpdateVertexCount(bool meshletReorderedVertices);

    std::vector<BoundingSphere>& GetMeshletBounds() { return m_meshletBounds; }

    void SetMeshletBoundsBufferView(std::unique_ptr<BufferView> view) { m_meshletBoundsBufferView = std::move(view); }
    const BufferView* GetMeshletBoundsBufferView() { return m_meshletBoundsBufferView.get(); }

    void SetMaterialDataIndex(unsigned int index);

    // CPU-only creation paths can call this later.
    void EnsureGpuBuffersCreated();

private:
    Mesh(std::shared_ptr<MeshSharedGeometry> geometry,
        const std::vector<UINT32>& indices,
        const std::shared_ptr<Material> material,
        unsigned int flags,
        bool createGpuBuffers);

    void CreateMeshlets(const std::vector<UINT32>& indices);
    void CreateMeshletReorderedVertices();
    void EnsureIndexBufferCreated();
    void ComputeBoundingSphere(const std::vector<UINT32>& indices);
    void ComputeAABB(DirectX::XMFLOAT3& min, DirectX::XMFLOAT3& max);
    static int GetNextGlobalIndex();

    static std::atomic<uint32_t> globalMeshCount;
    uint32_t m_globalMeshID;

    std::shared_ptr<MeshSharedGeometry> m_geom;
    std::vector<UINT32> m_cpuIndices; // kept so we can create the GPU IB later

    std::vector<meshopt_Meshlet> m_meshlets;
    std::vector<unsigned int> m_meshletVertices;
    std::vector<unsigned char> m_meshletTriangles;
    std::vector<BoundingSphere> m_meshletBounds;
    std::vector<std::byte> m_meshletReorderedVertices;
    std::vector<std::byte> m_meshletReorderedSkinningVertices;

    std::unique_ptr<BufferView> m_postSkinningVertexBufferView = nullptr;
    std::unique_ptr<BufferView> m_preSkinningVertexBufferView = nullptr;
    std::unique_ptr<BufferView> m_meshletBufferView = nullptr;
    std::unique_ptr<BufferView> m_meshletVerticesBufferView = nullptr;
    std::unique_ptr<BufferView> m_meshletTrianglesBufferView = nullptr;

    UINT m_indexCount = 0;
    std::shared_ptr<Buffer> m_indexBufferHandle;
    rhi::IndexBufferView m_indexBufferView{};
    bool m_gpuBuffersCreated = false;

    PerMeshCB m_perMeshBufferData = { 0 };
    unsigned int m_skinningVertexSize = 0;
    std::unique_ptr<BufferView> m_perMeshBufferView;
    std::unique_ptr<BufferView> m_meshletBoundsBufferView;
    MeshManager* m_pCurrentMeshManager = nullptr;

    std::shared_ptr<Skeleton> m_baseSkeleton = nullptr;
};
