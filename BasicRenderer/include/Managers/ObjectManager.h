#pragma once

#include <memory>
#include <optional>
#include <mutex>

#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Resources/Buffers/DynamicStructuredBuffer.h"
#include "ShaderBuffers.h"
#include "Resources/Buffers/SortedUnsignedIntBuffer.h"
#include "Render/IndirectCommand.h"
#include "Scene/Components.h"
#include "Interfaces/IResourceProvider.h"

class BufferView;
class DynamicBuffer;

class ObjectManager : public IResourceProvider {
public:
	static std::unique_ptr<ObjectManager> CreateUnique() {
		return std::unique_ptr<ObjectManager>(new ObjectManager());
	}
	Components::ObjectDrawInfo AddObject(const PerObjectCB& perObjectCB, const Components::OpaqueMeshInstances* opaqueInstances, const Components::AlphaTestMeshInstances* alphaTestInstances, const Components::BlendMeshInstances* blendInstances);
	void RemoveObject(const Components::ObjectDrawInfo* drawInfo);
	void UpdatePerObjectBuffer(BufferView*, PerObjectCB& data);
	void UpdateNormalMatrixBuffer(BufferView* view, void* data);
	std::shared_ptr<DynamicBuffer>& GetPerObjectBuffers() {
		return m_perObjectBuffers;
	}

	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
	std::vector<ResourceIdentifier> GetSupportedKeys() override;

private:
	ObjectManager();
	std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;
	std::shared_ptr<DynamicBuffer> m_perObjectBuffers; // Per object constant buffer
	std::shared_ptr<DynamicBuffer> m_masterIndirectCommandsBuffer; // Indirect draw command buffer
	std::shared_ptr<LazyDynamicStructuredBuffer<DirectX::XMFLOAT4X4>> m_normalMatrixBuffer; // Normal matrices for each object
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeOpaqueDrawSetIndices; // Indices into m_drawSetCommandsBuffer for active opaque objects
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeAlphaTestDrawSetIndices; // Indices into m_drawSetCommandsBuffer for active alpha tested objects
	std::shared_ptr<SortedUnsignedIntBuffer> m_activeBlendDrawSetIndices; // Indices into m_drawSetCommandsBuffer for active blended objects
	std::shared_ptr<LazyDynamicStructuredBuffer<PerMeshInstanceCB>> m_perMeshInstanceBuffers; // Indices into m_perObjectBuffers for each mesh instance in each object
	std::mutex m_objectUpdateMutex; // Mutex for thread safety
	std::mutex m_normalMatrixUpdateMutex; // Mutex for thread safety
};