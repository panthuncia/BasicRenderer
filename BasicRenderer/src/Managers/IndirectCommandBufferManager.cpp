#include "Managers/IndirectCommandBufferManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Resources/ResourceGroup.h"
#include "Resources/GloballyIndexedResource.h"
#include "Render/IndirectCommand.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "Resources/DynamicResource.h"

IndirectCommandBufferManager::IndirectCommandBufferManager() {
    m_parentResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_opaqueResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_alphaTestResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_blendResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_parentResourceGroup->AddResource(m_opaqueResourceGroup);
	m_parentResourceGroup->AddResource(m_alphaTestResourceGroup);
	m_parentResourceGroup->AddResource(m_blendResourceGroup);

	m_meshletCullingCommandResourceGroup = std::make_shared<ResourceGroup>(L"MeshletCullingCommandBuffers");

    // Initialize with one dummy draw
	UpdateBuffersForBucket(MaterialBuckets::Opaque, 1000);
	UpdateBuffersForBucket(MaterialBuckets::AlphaTest, 1000);
	UpdateBuffersForBucket(MaterialBuckets::Blend, 1000);
}

IndirectCommandBufferManager::~IndirectCommandBufferManager() {

	for (auto& pair : m_viewIDToBuffers) {
		auto& bufferComponent = pair.second;
		DeletionManager::GetInstance().MarkForDelete(bufferComponent.opaqueIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
		DeletionManager::GetInstance().MarkForDelete(bufferComponent.alphaTestIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
		DeletionManager::GetInstance().MarkForDelete(bufferComponent.blendIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
	}

	//DebugSharedPtrManager::GetInstance().StorePermenantly(m_clearBufferOpaque);
	//DebugSharedPtrManager::GetInstance().StorePermenantly(m_clearBufferAlphaTest);
	//DebugSharedPtrManager::GetInstance().StorePermenantly(m_clearBufferBlend);

 //   DeletionManager::GetInstance().MarkForDelete(m_clearBufferOpaque);
	//DeletionManager::GetInstance().MarkForDelete(m_clearBufferAlphaTest);
	//DeletionManager::GetInstance().MarkForDelete(m_clearBufferBlend);

	DeletionManager::GetInstance().MarkForDelete(m_parentResourceGroup);
}

// Add a single buffer to an existing ID
Components::IndirectCommandBuffers IndirectCommandBufferManager::CreateBuffersForView(uint64_t viewID) {

    auto opaqueBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_opaqueCommandBufferSize, sizeof(DispatchMeshIndirectCommand), false, true, true);
    opaqueBuffer->SetName(L"OpaqueIndirectCommandBuffer ("+std::to_wstring(viewID)+L")");
    std::shared_ptr<DynamicGloballyIndexedResource> pOpaqueDynamicResource = std::make_shared<DynamicGloballyIndexedResource>(opaqueBuffer);
	m_opaqueResourceGroup->AddResource(opaqueBuffer);

	auto alphaTestBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_alphaTestCommandBufferSize, sizeof(DispatchMeshIndirectCommand), false, true, true);
	alphaTestBuffer->SetName(L"AlphaTestIndirectCommandBuffer (" + std::to_wstring(viewID) + L")");
	std::shared_ptr<DynamicGloballyIndexedResource> pAlphaTestDynamicResource = std::make_shared<DynamicGloballyIndexedResource>(alphaTestBuffer);
	m_alphaTestResourceGroup->AddResource(alphaTestBuffer);

	auto blendBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_blendCommandBufferSize, sizeof(DispatchMeshIndirectCommand), false, true, true);
	blendBuffer->SetName(L"BlendIndirectCommandBuffer (" + std::to_wstring(viewID) + L")");
	std::shared_ptr<DynamicGloballyIndexedResource> pBlendDynamicResource = std::make_shared<DynamicGloballyIndexedResource>(blendBuffer);
	m_blendResourceGroup->AddResource(blendBuffer);

	auto meshletFrustrumCullingBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_blendCommandBufferSize, sizeof(DispatchIndirectCommand), false, true, true);
	meshletFrustrumCullingBuffer->SetName(L"MeshletFrustrumCullingIndirectCommandBuffer (" + std::to_wstring(viewID) + L")");
	std::shared_ptr<DynamicGloballyIndexedResource> pMeshletFrustrumCullingDynamicResource = std::make_shared<DynamicGloballyIndexedResource>(meshletFrustrumCullingBuffer);
	m_meshletCullingCommandResourceGroup->AddResource(meshletFrustrumCullingBuffer);

    Components::IndirectCommandBuffers bufferComponent;
	bufferComponent.opaqueIndirectCommandBuffer = pOpaqueDynamicResource;
	bufferComponent.alphaTestIndirectCommandBuffer = pAlphaTestDynamicResource;
	bufferComponent.blendIndirectCommandBuffer = pBlendDynamicResource;
	bufferComponent.meshletFrustrumCullingIndirectCommandBuffer = pMeshletFrustrumCullingDynamicResource;

	m_viewIDToBuffers[viewID] = bufferComponent;

	return bufferComponent;
}

// Remove buffers associated with an ID
void IndirectCommandBufferManager::UnregisterBuffers(uint64_t viewID) {
	auto& bufferComponent = m_viewIDToBuffers[viewID];
	DeletionManager::GetInstance().MarkForDelete(bufferComponent.opaqueIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
	DeletionManager::GetInstance().MarkForDelete(bufferComponent.alphaTestIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
	DeletionManager::GetInstance().MarkForDelete(bufferComponent.blendIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
	DeletionManager::GetInstance().MarkForDelete(bufferComponent.meshletFrustrumCullingIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame

	m_opaqueResourceGroup->RemoveResource(bufferComponent.opaqueIndirectCommandBuffer->GetResource().get());
	m_alphaTestResourceGroup->RemoveResource(bufferComponent.alphaTestIndirectCommandBuffer->GetResource().get());
	m_blendResourceGroup->RemoveResource(bufferComponent.blendIndirectCommandBuffer->GetResource().get());

	m_meshletCullingCommandResourceGroup->RemoveResource(bufferComponent.meshletFrustrumCullingIndirectCommandBuffer->GetResource().get());

	m_viewIDToBuffers.erase(viewID);
}

// Update all buffers when the number of draws changes
void IndirectCommandBufferManager::UpdateBuffersForBucket(MaterialBuckets bucket, unsigned int numDraws) {

    // Update clear buffers
    switch (bucket) {
    case MaterialBuckets::Opaque: {
        unsigned int newSize = ((numDraws + m_incrementSize - 1) / m_incrementSize) * m_incrementSize;
        if (newSize <= m_opaqueCommandBufferSize) {
            return;
        }
        m_opaqueCommandBufferSize = newSize;
        //auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_opaqueCommandBufferSize, sizeof(DispatchMeshIndirectCommand), ResourceState::ALL_SRV, false, true, true);
        //DeletionManager::GetInstance().MarkForDelete(m_clearBufferOpaque); // Delay deletion until after the current frame
        //m_clearBufferOpaque = resource;
        //m_clearBufferOpaque->SetName(L"ClearBufferOpaque");
        break;
    }
    case MaterialBuckets::AlphaTest: {
        unsigned int newSize = ((numDraws + m_incrementSize - 1) / m_incrementSize) * m_incrementSize;
        if (newSize <= m_alphaTestCommandBufferSize) {
            return;
        }
        m_alphaTestCommandBufferSize = newSize;
        //auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_alphaTestCommandBufferSize, sizeof(DispatchMeshIndirectCommand), ResourceState::COPY_SOURCE, false, true, true);
        //DeletionManager::GetInstance().MarkForDelete(m_clearBufferAlphaTest); // Delay deletion until after the current frame
        //m_clearBufferAlphaTest = resource;
        //m_clearBufferAlphaTest->SetName(L"ClearBufferAlphaTest");
        break;
    }
    case MaterialBuckets::Blend: {
        unsigned int newSize = ((numDraws + m_incrementSize - 1) / m_incrementSize) * m_incrementSize;
        if (newSize <= m_blendCommandBufferSize) {
            return;
        }
        m_blendCommandBufferSize = newSize;
        //auto resource = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_blendCommandBufferSize, sizeof(DispatchMeshIndirectCommand), ResourceState::COPY_SOURCE, false, true, true);
        //DeletionManager::GetInstance().MarkForDelete(m_clearBufferBlend); // Delay deletion until after the current frame
        //m_clearBufferBlend = resource;
        //m_clearBufferBlend->SetName(L"ClearBufferBlend");
        break;
    }
    }
	m_totalIndirectCommands = m_opaqueCommandBufferSize + m_alphaTestCommandBufferSize + m_blendCommandBufferSize;
    // Update per-view buffers
    auto& deletionManager = DeletionManager::GetInstance();

    for (auto& pair : m_viewIDToBuffers) {
		auto& bufferComponent = pair.second;
		switch (bucket) {
		case MaterialBuckets::Opaque:
			deletionManager.MarkForDelete(bufferComponent.opaqueIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
			bufferComponent.opaqueIndirectCommandBuffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_opaqueCommandBufferSize, sizeof(DispatchMeshIndirectCommand), false, true, true));
			break;
		case MaterialBuckets::AlphaTest:
			bufferComponent.alphaTestIndirectCommandBuffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_alphaTestCommandBufferSize, sizeof(DispatchMeshIndirectCommand), false, true, true));
			break;
		case MaterialBuckets::Blend:
			bufferComponent.blendIndirectCommandBuffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_blendCommandBufferSize, sizeof(DispatchMeshIndirectCommand), false, true, true));
			break;
		}
		bufferComponent.meshletFrustrumCullingIndirectCommandBuffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_totalIndirectCommands, sizeof(DispatchIndirectCommand), false, true, true));
    }
}

void IndirectCommandBufferManager::SetIncrementSize(unsigned int incrementSize) {
    m_incrementSize = incrementSize;
}
