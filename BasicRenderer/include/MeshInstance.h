#pragma once

#include <memory>
#include "Mesh.h"
#include "Skeleton.h"

class MeshInstance {
public:
    MeshInstance(std::shared_ptr<Mesh> mesh)
        : m_mesh(mesh) {}

    void SetPostSkinningVertexBufferView(std::unique_ptr<BufferView> view);
    BufferView* GetPostSkinningVertexBufferView();

	void SetBufferViews(std::unique_ptr<BufferView> postSkinningVertexBufferView);

    void SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
        m_skeleton = skeleton;
    }

    std::shared_ptr<Skeleton> GetSkin() const {
        return m_skeleton;
    }

    std::shared_ptr<Mesh> GetMesh() const {
        return m_mesh;
    }

    unsigned int GetPostSkinningVertexBufferOffset() const {
        return m_postSkinningVertexBufferView->GetOffset();
    }

    bool HasSkin() const { return m_skeleton != nullptr; }

    void SetCurrentMeshManager(MeshManager* manager) {
        m_pCurrentMeshManager = manager;
    }

private:
	PerMeshInstanceCB m_perMeshInstanceBufferData;
    std::shared_ptr<Mesh> m_mesh;
    std::shared_ptr<Skeleton> m_skeleton; // Instance-specific skeleton
    MeshManager* m_pCurrentMeshManager = nullptr;
    std::unique_ptr<BufferView> m_postSkinningVertexBufferView = nullptr;
    std::unique_ptr<BufferView> m_perMeshInstanceBufferView;
};