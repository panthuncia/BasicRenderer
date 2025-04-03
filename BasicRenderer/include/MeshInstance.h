#pragma once

#include <memory>
#include "Mesh.h"
#include "Skeleton.h"

class MeshInstance {
public:

	static std::shared_ptr<MeshInstance> CreateShared(std::shared_ptr<Mesh> mesh) {
		return std::shared_ptr<MeshInstance>(new MeshInstance(mesh));
	}
    static std::unique_ptr<MeshInstance> CreateUnique(std::shared_ptr<Mesh> mesh) {
        return std::unique_ptr<MeshInstance>(new MeshInstance(mesh));
    }

    //void SetPostSkinningVertexBufferView(std::unique_ptr<BufferView> view);
    BufferView* GetPostSkinningVertexBufferView();

	void SetBufferViews(std::unique_ptr<BufferView> postSkinningVertexBufferView, std::unique_ptr<BufferView> perMeshInstanceBufferView);
    void SetBufferViewUsingBaseMesh(std::unique_ptr<BufferView> perMeshInstanceBufferView);

    void SetSkeleton(std::shared_ptr<Skeleton> skeleton);

    std::shared_ptr<Skeleton> GetSkin() const {
        return m_skeleton;
    }

    std::shared_ptr<Mesh>& GetMesh() {
        return m_mesh;
    }

    unsigned int GetPostSkinningVertexBufferOffset() const {
        return m_postSkinningVertexBufferView->GetOffset();
    }

	unsigned int GetPerMeshInstanceBufferOffset() const {
		return m_perMeshInstanceBufferView->GetOffset();
	}

    bool HasSkin() const { return m_skeleton != nullptr; }

    void SetCurrentMeshManager(MeshManager* manager) {
        m_pCurrentMeshManager = manager;
    }

	const PerMeshInstanceCB& GetPerMeshInstanceBufferData() const {
		return m_perMeshInstanceBufferData;
	}

	void SetAnimationSpeed(float speed) {
		m_animationSpeed = speed;
		if (m_skeleton != nullptr) {
			m_skeleton->SetAnimationSpeed(speed);
		}
	}

private:
    MeshInstance(std::shared_ptr<Mesh> mesh)
        : m_mesh(mesh) {
        if (mesh->HasBaseSkin()) {
			SetSkeleton(mesh->GetBaseSkin());
        }
    }
	PerMeshInstanceCB m_perMeshInstanceBufferData;
    std::shared_ptr<Mesh> m_mesh;
    std::shared_ptr<Skeleton> m_skeleton; // Instance-specific skeleton
    MeshManager* m_pCurrentMeshManager = nullptr;
    std::unique_ptr<BufferView> m_postSkinningVertexBufferView = nullptr;
    std::unique_ptr<BufferView> m_perMeshInstanceBufferView;
	float m_animationSpeed = 1.0f;
};