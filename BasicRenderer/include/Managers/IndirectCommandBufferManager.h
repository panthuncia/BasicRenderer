#pragma once
#include <vector>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "Materials/MaterialBuckets.h"

class DynamicGloballyIndexedResource;
class ResourceGroup;
class Buffer;

class IndirectCommandBufferManager {
public:
	~IndirectCommandBufferManager();
	static std::unique_ptr<IndirectCommandBufferManager> CreateUnique() {
		return std::unique_ptr<IndirectCommandBufferManager>(new IndirectCommandBufferManager());
	}

	static std::shared_ptr<IndirectCommandBufferManager> CreateShared() {
		return std::shared_ptr<IndirectCommandBufferManager>(new IndirectCommandBufferManager());
	}

    // Add a single buffer to an existing ID
    std::shared_ptr<DynamicGloballyIndexedResource> CreateBuffer(uint64_t entityID, MaterialBuckets bucket);

    // Remove buffers associated with an ID
    void UnregisterBuffers(uint64_t entityID);

    // Update all buffers when the number of draws changes
    void UpdateBuffersForBucket(MaterialBuckets bucket, unsigned int numDraws);

    void SetIncrementSize(unsigned int incrementSize);

	std::shared_ptr<ResourceGroup> GetResourceGroup() { return m_parentResourceGroup; }

	//std::shared_ptr<Buffer>& GetOpaqueClearBuffer() { return m_clearBufferOpaque; }
	//std::shared_ptr<Buffer>& GetAlphaTestClearBuffer() { return m_clearBufferAlphaTest; }
	//std::shared_ptr<Buffer>& GetBlendClearBuffer() { return m_clearBufferBlend; }

private:
    IndirectCommandBufferManager();
    std::unordered_map<MaterialBuckets, std::unordered_map<int, std::vector<std::shared_ptr<DynamicGloballyIndexedResource>>>> m_buffers;
	unsigned int m_opaqueCommandBufferSize = 0;
	unsigned int m_alphaTestCommandBufferSize = 0;
	unsigned int m_blendCommandBufferSize = 0;
    unsigned int m_incrementSize = 1000; // TODO: Values are small for testing
	std::shared_ptr<ResourceGroup> m_alphaTestResourceGroup;
	std::shared_ptr<ResourceGroup> m_opaqueResourceGroup;
	std::shared_ptr<ResourceGroup> m_blendResourceGroup;
	std::shared_ptr<ResourceGroup> m_parentResourceGroup;
	//std::shared_ptr<Buffer> m_clearBufferOpaque;
	//std::shared_ptr<Buffer> m_clearBufferAlphaTest;
	//std::shared_ptr<Buffer> m_clearBufferBlend;
};