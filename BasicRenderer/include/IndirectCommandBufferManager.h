#pragma once
#include <vector>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "MaterialBuckets.h"

class DynamicGloballyIndexedResource;
class ResourceGroup;
class Buffer;

class IndirectCommandBufferManager {
public:
	static std::unique_ptr<IndirectCommandBufferManager> CreateUnique() {
		return std::unique_ptr<IndirectCommandBufferManager>(new IndirectCommandBufferManager());
	}
    // Add a single buffer to an existing ID
    std::shared_ptr<DynamicGloballyIndexedResource> CreateBuffer(const int entityID, MaterialBuckets bucket);

    // Remove buffers associated with an ID
    void UnregisterBuffers(const int entityID);

    // Update all buffers when the number of draws changes
    void UpdateBuffersForBucket(MaterialBuckets bucket, unsigned int numDraws);

    void SetIncrementSize(unsigned int incrementSize);

	std::shared_ptr<ResourceGroup> GetResourceGroup() { return m_parentResourceGroup; }

	std::shared_ptr<Buffer>& GetOpaqueClearBuffer() { return m_clearBufferOpaque; }
	std::shared_ptr<Buffer>& GetTransparentClearBuffer() { return m_clearBufferTransparent; }

	Microsoft::WRL::ComPtr<ID3D12CommandSignature> GetCommandSignature() { return m_commandSignature; }

private:
    IndirectCommandBufferManager();
    std::unordered_map<MaterialBuckets, std::unordered_map<int, std::vector<std::shared_ptr<DynamicGloballyIndexedResource>>>> m_buffers;
	unsigned int m_opaqueCommandBufferSize = 1; // TODO: Values are small for testing
	unsigned int m_transparentCommandBufferSize = 1;
    unsigned int m_incrementSize = 1;
	std::shared_ptr<ResourceGroup> m_transparentResourceGroup;
	std::shared_ptr<ResourceGroup> m_opaqueResourceGroup;
	std::shared_ptr<ResourceGroup> m_parentResourceGroup;
	std::shared_ptr<Buffer> m_clearBufferOpaque;
	std::shared_ptr<Buffer> m_clearBufferTransparent;
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_commandSignature;
};