#pragma once

#include <memory>
#include "Mesh/Mesh.h"
#include "Animation/Skeleton.h"

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
	BufferView* GetPerMeshInstanceBufferView() { return m_perMeshInstanceBufferView.get(); }

	void SetBufferViews(std::unique_ptr<BufferView> postSkinningVertexBufferView, std::unique_ptr<BufferView> perMeshInstanceBufferView, std::unique_ptr<BufferView> meshletBoundsBufferView);
    void SetBufferViewUsingBaseMesh(std::unique_ptr<BufferView> perMeshInstanceBufferView);

    void SetSkeleton(std::shared_ptr<Skeleton> skeleton);

    std::shared_ptr<Skeleton> GetSkin() const {
        return m_skeleton;
    }

    std::shared_ptr<Mesh>& GetMesh() {
        return m_mesh;
    }

    uint64_t GetPostSkinningVertexBufferOffset() const {
        return m_postSkinningVertexBufferView->GetOffset();
    }

	uint64_t GetPerMeshInstanceBufferOffset() const {
		return m_perMeshInstanceBufferView->GetOffset();
	}

    bool HasSkin() const { return m_skeleton != nullptr; }

    void SetCurrentMeshManager(MeshManager* manager) {
        m_pCurrentMeshManager = manager;
    }

	const PerMeshInstanceCB& GetPerMeshInstanceBufferData() const {
		return m_perMeshInstanceBufferData;
	}

    void SetMeshletBitfieldBufferView(std::unique_ptr<BufferView> meshletBitfieldBufferView);
    void SetClusterToVisibleClusterIndicesBufferView(std::unique_ptr<BufferView> clusterIndicesBufferView);

	void SetAnimationSpeed(float speed) {
		m_animationSpeed = speed;
		if (m_skeleton != nullptr) {
			m_skeleton->SetAnimationSpeed(speed);
		}
	}

    void SetMeshletBoundsBufferView(std::unique_ptr<BufferView> view) {
        m_meshletBoundsBufferView = std::move(view);
    }

    void SetMeshletBoundsFromBaseMesh() {
		if (m_mesh->GetMeshletBoundsBufferView() != nullptr) {
			
		}
    }

    BufferView* GetMeshletBoundsBufferView() {
        return m_meshletBoundsBufferView.get();
    }

    void SetPerObjectBufferIndex(uint32_t index);
    void SetPerMeshBufferIndex(uint32_t index);
	void SetSkinningInstanceSlot(uint32_t slot);

    void SetCLodBufferViews(
        std::unique_ptr<BufferView> perMeshInstanceClodOffsetsView
    ) {
        m_perMeshInstanceClodOffsetsView = std::move(perMeshInstanceClodOffsetsView);
    }


    const BufferView* GetCLodOffsetsView() const {
        return m_perMeshInstanceClodOffsetsView.get();
    }

private:
    MeshInstance(std::shared_ptr<Mesh> mesh)
        : m_mesh(mesh) {
        if (mesh->HasBaseSkin()) {
            SetSkeleton(mesh->GetBaseSkin()->CopySkeleton());
        }
    }
    PerMeshInstanceCB m_perMeshInstanceBufferData = {};
    std::shared_ptr<Mesh> m_mesh;
    std::shared_ptr<Skeleton> m_skeleton; // Instance-specific skeleton
    MeshManager* m_pCurrentMeshManager = nullptr;
    std::unique_ptr<BufferView> m_postSkinningVertexBufferView = nullptr;
    std::unique_ptr<BufferView> m_perMeshInstanceBufferView;
	std::unique_ptr<BufferView> m_meshletBitfieldBufferView = nullptr;
    std::unique_ptr<BufferView> m_meshletBoundsBufferView = nullptr;
	std::unique_ptr<BufferView> m_clusterToVisibleClusterIndicesBufferView = nullptr;

    std::unique_ptr<BufferView> m_perMeshInstanceClodOffsetsView = nullptr;

	float m_animationSpeed = 1.0f;
};