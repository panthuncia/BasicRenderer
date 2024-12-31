#pragma once

#include <memory>

#include "LazyDynamicStructuredBuffer.h"
#include "DynamicStructuredBuffer.h"
#include "buffers.h"
#include "SortedUnsignedIntBuffer.h"
#include "IndirectCommand.h"

class RenderableObject;
class BufferView;
class DynamicBuffer;

class ObjectManager {
public:
	static std::unique_ptr<ObjectManager> CreateUnique() {
		return std::unique_ptr<ObjectManager>(new ObjectManager());
	}
	void AddObject(std::shared_ptr<RenderableObject>& object);
	void RemoveObject(std::shared_ptr<RenderableObject>& object);
	unsigned int GetPerObjectBufferSRVIndex() const {
		return m_perObjectBuffers->GetSRVInfo().index;
	}
	void UpdateSkinning(RenderableObject* object);
	void UpdatePerObjectBuffer(BufferView*, PerObjectCB& data);
	void UpdatePreSkinningNormalMatrixBuffer(BufferView* view, void* data);
	void UpdatePostSkinningNormalMatrixBuffer(BufferView* view, void* data);
	std::shared_ptr<DynamicBuffer>& GetPerObjectBuffers() {
		return m_perObjectBuffers;
	}

	unsigned int GetOpaqueDrawSetCommandsBufferSRVIndex() const {
		return m_opaqueDrawSetCommandsBuffer->GetSRVInfo().index;
	}
	
	unsigned int GetAlphaTestDrawSetCommandsBufferSRVIndex() const {
		return m_alphaTestDrawSetCommandsBuffer->GetSRVInfo().index;
	}

	unsigned int GetBlendDrawSetCommandsBufferSRVIndex() const {
		return m_blendDrawSetCommandsBuffer->GetSRVInfo().index;
	}

	unsigned int GetActiveOpaqueDrawSetIndicesBufferSRVIndex() const {
		return m_activeOpaqueDrawSetIndices->GetSRVInfo().index;
	}

	unsigned int GetActiveAlphaTestDrawSetIndicesBufferSRVIndex() const {
		return m_activeAlphaTestDrawSetIndices->GetSRVInfo().index;
	}

	unsigned int GetActiveBlendDrawSetIndicesBufferSRVIndex() const {
		return m_activeBlendDrawSetIndices->GetSRVInfo().index;
	}

	unsigned int GetPreSkinningNormalMatrixBufferSRVIndex() const {
		return m_preSkinningNormalMatrixBuffer->GetSRVInfo().index;
	}

	unsigned int GetPostSkinningNormalMatrixBufferSRVIndex() const {
		return m_postSkinningNormalMatrixBuffer->GetSRVInfo().index;
	}

	unsigned int GetPostSkinningNormalMatrixBufferUAVIndex() const {
		return m_postSkinningNormalMatrixBuffer->GetUAVShaderVisibleInfo().index;
	}

	std::shared_ptr<DynamicBuffer>& GetOpaqueDrawSetCommandsBuffer() {
		return m_opaqueDrawSetCommandsBuffer;
	}

	std::shared_ptr<DynamicBuffer>& GetAlphaTestDrawSetCommandsBuffer() {
		return m_alphaTestDrawSetCommandsBuffer;
	}

	std::shared_ptr<DynamicBuffer>& GetBlendDrawSetCommandsBuffer() {
		return m_blendDrawSetCommandsBuffer;
	}

	std::shared_ptr<SortedUnsignedIntBuffer>& GetActiveOpaqueDrawSetIndices() {
		return m_activeOpaqueDrawSetIndices;
	}

	std::shared_ptr<SortedUnsignedIntBuffer>& GetActiveAlphaTestDrawSetIndices() {
		return m_activeAlphaTestDrawSetIndices;
	}

	std::shared_ptr<SortedUnsignedIntBuffer>& GetActiveBlendDrawSetIndices() {
		return m_activeBlendDrawSetIndices;
	}

	std::shared_ptr<LazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>>& GetPreSkinningNormalMatrixBuffer() {
		return m_preSkinningNormalMatrixBuffer;
	}

	std::shared_ptr<LazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>>& GetPostSkinningNormalMatrixBuffer() {
		return m_postSkinningNormalMatrixBuffer;
	}

private:
	ObjectManager();
	std::vector<std::shared_ptr<RenderableObject>> m_objects;
	std::shared_ptr<DynamicBuffer> m_perObjectBuffers;
	std::shared_ptr<DynamicBuffer> m_opaqueDrawSetCommandsBuffer;
	std::shared_ptr<DynamicBuffer> m_alphaTestDrawSetCommandsBuffer;
	std::shared_ptr<DynamicBuffer> m_blendDrawSetCommandsBuffer;
	std::shared_ptr<LazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>> m_preSkinningNormalMatrixBuffer;
	std::shared_ptr<LazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>> m_postSkinningNormalMatrixBuffer;
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeOpaqueDrawSetIndices;
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeAlphaTestDrawSetIndices;
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeBlendDrawSetIndices;
};