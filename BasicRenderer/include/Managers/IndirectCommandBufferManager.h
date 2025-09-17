#pragma once
#include <vector>
#include <unordered_map>
#include <spdlog/spdlog.h>

#include "Materials/MaterialBuckets.h"
#include "Scene/Components.h"
#include "Interfaces/IResourceProvider.h"

class DynamicGloballyIndexedResource;
class ResourceGroup;
class Buffer;

class IndirectCommandBufferManager : public IResourceProvider {
public:
	~IndirectCommandBufferManager();
	static std::unique_ptr<IndirectCommandBufferManager> CreateUnique() {
		return std::unique_ptr<IndirectCommandBufferManager>(new IndirectCommandBufferManager());
	}

	static std::shared_ptr<IndirectCommandBufferManager> CreateShared() {
		return std::shared_ptr<IndirectCommandBufferManager>(new IndirectCommandBufferManager());
	}

    // Add a single buffer to an existing ID
    Components::IndirectCommandBuffers CreateBuffersForView(uint64_t viewID);

    // Remove buffers associated with an ID
    void UnregisterBuffers(uint64_t viewID);

    // Update all buffers when the number of draws changes
    void UpdateBuffersForBucket(MaterialBuckets bucket, unsigned int numDraws);

    void SetIncrementSize(unsigned int incrementSize);

	//std::shared_ptr<Buffer>& GetOpaqueClearBuffer() { return m_clearBufferOpaque; }
	//std::shared_ptr<Buffer>& GetAlphaTestClearBuffer() { return m_clearBufferAlphaTest; }
	//std::shared_ptr<Buffer>& GetBlendClearBuffer() { return m_clearBufferBlend; }

	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
	std::vector<ResourceIdentifier> GetSupportedKeys() override;

private:
    IndirectCommandBufferManager();
	std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;
	std::unordered_map<uint64_t, Components::IndirectCommandBuffers> m_viewIDToBuffers;
	unsigned int m_opaqueCommandBufferSize = 0;
	unsigned int m_alphaTestCommandBufferSize = 0;
	unsigned int m_blendCommandBufferSize = 0;
	unsigned int m_totalIndirectCommands = 0;
    unsigned int m_incrementSize = 1000; // TODO: Values are small for testing
	std::shared_ptr<ResourceGroup> m_alphaTestResourceGroup;
	std::shared_ptr<ResourceGroup> m_opaqueResourceGroup;
	std::shared_ptr<ResourceGroup> m_blendResourceGroup;
	//std::shared_ptr<ResourceGroup> m_parentResourceGroup;

	std::shared_ptr<ResourceGroup> m_meshletCullingCommandResourceGroup;
	//std::shared_ptr<Buffer> m_clearBufferOpaque;
	//std::shared_ptr<Buffer> m_clearBufferAlphaTest;
	//std::shared_ptr<Buffer> m_clearBufferBlend;
};