#pragma once

#include <memory>
#include <optional>

#include "LazyDynamicStructuredBuffer.h"
#include "DynamicStructuredBuffer.h"
#include "buffers.h"
#include "SortedUnsignedIntBuffer.h"
#include "IndirectCommand.h"
#include "Components.h"

class RenderableObject;
class BufferView;
class DynamicBuffer;

class ObjectManager {
public:
	static std::unique_ptr<ObjectManager> CreateUnique() {
		return std::unique_ptr<ObjectManager>(new ObjectManager());
	}
	Components::ObjectDrawInfo AddObject(PerObjectCB& perObjectCB, const Components::OpaqueMeshInstances* opaqueInstances, const Components::AlphaTestMeshInstances* alphaTestInstances, const Components::BlendMeshInstances* blendInstances);
	void RemoveObject(const Components::ObjectDrawInfo* drawInfo);
	void UpdatePerObjectBuffer(BufferView*, PerObjectCB& data);
	void UpdateNormalMatrixBuffer(BufferView* view, void* data);
	std::shared_ptr<DynamicBuffer>& GetPerObjectBuffers() {
		return m_perObjectBuffers;
	}

	unsigned int GetPerObjectBufferSRVIndex() const;

	unsigned int GetMasterIndirectCommandsBufferSRVIndex() const;

	unsigned int GetActiveOpaqueDrawSetIndicesBufferSRVIndex() const;

	unsigned int GetActiveAlphaTestDrawSetIndicesBufferSRVIndex() const;

	unsigned int GetActiveBlendDrawSetIndicesBufferSRVIndex() const;

	unsigned int GetNormalMatrixBufferSRVIndex() const;

	std::shared_ptr<SortedUnsignedIntBuffer>& GetActiveOpaqueDrawSetIndices() {
		return m_activeOpaqueDrawSetIndices;
	}

	std::shared_ptr<SortedUnsignedIntBuffer>& GetActiveAlphaTestDrawSetIndices() {
		return m_activeAlphaTestDrawSetIndices;
	}

	std::shared_ptr<SortedUnsignedIntBuffer>& GetActiveBlendDrawSetIndices() {
		return m_activeBlendDrawSetIndices;
	}

	std::shared_ptr<LazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>>& GetNormalMatrixBuffer() {
		return m_normalMatrixBuffer;
	}


private:
	ObjectManager();
	std::vector<std::shared_ptr<RenderableObject>> m_objects;
	std::shared_ptr<DynamicBuffer> m_perObjectBuffers; // Per object constant buffer
	std::shared_ptr<DynamicBuffer> m_masterIndirectCommandsBuffer; // Indirect draw command buffer
	std::shared_ptr<LazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>> m_normalMatrixBuffer; // Normal matrices for each object
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeOpaqueDrawSetIndices; // Indices into m_drawSetCommandsBuffer for active opaque objects
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeAlphaTestDrawSetIndices; // Indices into m_drawSetCommandsBuffer for active alpha tested objects
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeBlendDrawSetIndices; // Indices into m_drawSetCommandsBuffer for active blended objects
	std::shared_ptr<LazyDynamicStructuredBuffer<PerMeshInstanceCB>> m_perMeshInstanceBuffers; // Indices into m_perObjectBuffers for each mesh instance in each object
};