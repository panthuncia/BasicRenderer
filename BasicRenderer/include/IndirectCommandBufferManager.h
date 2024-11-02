#pragma once
#include <vector>
#include <unordered_map>
#include <spdlog/spdlog.h>

class DynamicGloballyIndexedResource;
class ResourceGroup;

class IndirectCommandBufferManager {
public:
	static std::unique_ptr<IndirectCommandBufferManager> CreateUnique() {
		return std::unique_ptr<IndirectCommandBufferManager>(new IndirectCommandBufferManager());
	}
    // Add a single buffer to an existing ID
    std::shared_ptr<DynamicGloballyIndexedResource> CreateBuffer(const int entityID);

    // Remove buffers associated with an ID
    void UnregisterBuffers(const int entityID);

    // Update all buffers when the number of draws changes
    void UpdateAllBuffers(unsigned int numDraws);

    void SetIncrementSize(unsigned int incrementSize);

	std::shared_ptr<ResourceGroup> GetResourceGroup() { return m_resourceGroup; }

private:
    IndirectCommandBufferManager();
    std::unordered_map<int, std::vector<std::shared_ptr<DynamicGloballyIndexedResource>>> m_buffers;
	unsigned int m_commandBufferSize = 1; // TODO: Values are small for testing
    unsigned int m_incrementSize = 1;
	std::shared_ptr<ResourceGroup> m_resourceGroup;
};