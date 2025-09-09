#include "Managers/IndirectCommandBufferManager.h"

#include "Managers/Singletons/ResourceManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Resources/ResourceGroup.h"
#include "Resources/GloballyIndexedResource.h"
#include "Render/IndirectCommand.h"
#include "Managers/Singletons/PSOManager.h"
#include "Managers/Singletons/CommandSignatureManager.h"
#include "Resources/DynamicResource.h"
#include "../../generated/BuiltinResources.h"

IndirectCommandBufferManager::IndirectCommandBufferManager() {
    //m_parentResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_opaqueResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_alphaTestResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	m_blendResourceGroup = std::make_shared<ResourceGroup>(L"IndirectCommandBuffers");
	//m_parentResourceGroup->AddResource(m_opaqueResourceGroup);
	//m_parentResourceGroup->AddResource(m_alphaTestResourceGroup);
	//m_parentResourceGroup->AddResource(m_blendResourceGroup);

	m_meshletCullingCommandResourceGroup = std::make_shared<ResourceGroup>(L"MeshletCullingCommandBuffers");

    // Initialize with one dummy draw
	UpdateBuffersForBucket(MaterialBuckets::Opaque, 1000);
	UpdateBuffersForBucket(MaterialBuckets::AlphaTest, 1000);
	UpdateBuffersForBucket(MaterialBuckets::Blend, 1000);

	m_resources[Builtin::IndirectCommandBuffers::Opaque] = m_opaqueResourceGroup;
	m_resources[Builtin::IndirectCommandBuffers::AlphaTest] = m_alphaTestResourceGroup;
	m_resources[Builtin::IndirectCommandBuffers::Blend] = m_blendResourceGroup;
	m_resources[Builtin::IndirectCommandBuffers::MeshletCulling] = m_meshletCullingCommandResourceGroup;
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

	//DeletionManager::GetInstance().MarkForDelete(m_parentResourceGroup);
}

// Add a single buffer to an existing ID
Components::IndirectCommandBuffers IndirectCommandBufferManager::CreateBuffersForView(uint64_t viewID) {

    auto opaqueBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_opaqueCommandBufferSize, sizeof(DispatchMeshIndirectCommand), true, true);
    opaqueBuffer->SetName(L"OpaqueIndirectCommandBuffer ("+std::to_wstring(viewID)+L")");
    std::shared_ptr<DynamicGloballyIndexedResource> pOpaqueDynamicResource = std::make_shared<DynamicGloballyIndexedResource>(opaqueBuffer);
	m_opaqueResourceGroup->AddResource(pOpaqueDynamicResource);

	auto alphaTestBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_alphaTestCommandBufferSize, sizeof(DispatchMeshIndirectCommand), true, true);
	alphaTestBuffer->SetName(L"AlphaTestIndirectCommandBuffer (" + std::to_wstring(viewID) + L")");
	std::shared_ptr<DynamicGloballyIndexedResource> pAlphaTestDynamicResource = std::make_shared<DynamicGloballyIndexedResource>(alphaTestBuffer);
	m_alphaTestResourceGroup->AddResource(pAlphaTestDynamicResource);

	auto blendBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_blendCommandBufferSize, sizeof(DispatchMeshIndirectCommand), true, true);
	blendBuffer->SetName(L"BlendIndirectCommandBuffer (" + std::to_wstring(viewID) + L")");
	std::shared_ptr<DynamicGloballyIndexedResource> pBlendDynamicResource = std::make_shared<DynamicGloballyIndexedResource>(blendBuffer);
	m_blendResourceGroup->AddResource(pBlendDynamicResource);

	auto meshletFrustrumCullingBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_totalIndirectCommands, sizeof(DispatchIndirectCommand), true, true);
	meshletFrustrumCullingBuffer->SetName(L"MeshletFrustrumCullingIndirectCommandBuffer (" + std::to_wstring(viewID) + L")");
	std::shared_ptr<DynamicGloballyIndexedResource> pMeshletFrustrumCullingDynamicResource = std::make_shared<DynamicGloballyIndexedResource>(meshletFrustrumCullingBuffer);
	m_meshletCullingCommandResourceGroup->AddResource(pMeshletFrustrumCullingDynamicResource);

	auto meshletOcclusionCullingBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_totalIndirectCommands, sizeof(DispatchIndirectCommand), true, true);
	meshletOcclusionCullingBuffer->SetName(L"MeshletOcclusionCullingIndirectCommandBuffer (" + std::to_wstring(viewID) + L")");
	std::shared_ptr<DynamicGloballyIndexedResource> pMeshletOcclusionCullingDynamicResource = std::make_shared<DynamicGloballyIndexedResource>(meshletOcclusionCullingBuffer);
	m_meshletCullingCommandResourceGroup->AddResource(pMeshletOcclusionCullingDynamicResource);

	auto meshletCullingResetBuffer = ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_totalIndirectCommands, sizeof(DispatchIndirectCommand), true, true);
	meshletCullingResetBuffer->SetName(L"MeshletCullingResetIndirectCommandBuffer (" + std::to_wstring(viewID) + L")");
	std::shared_ptr<DynamicGloballyIndexedResource> pMeshletFrustrumCullingResetDynamicResource = std::make_shared<DynamicGloballyIndexedResource>(meshletCullingResetBuffer);
	m_meshletCullingCommandResourceGroup->AddResource(pMeshletFrustrumCullingResetDynamicResource);

    Components::IndirectCommandBuffers bufferComponent;
	bufferComponent.opaqueIndirectCommandBuffer = pOpaqueDynamicResource;
	bufferComponent.alphaTestIndirectCommandBuffer = pAlphaTestDynamicResource;
	bufferComponent.blendIndirectCommandBuffer = pBlendDynamicResource;
	bufferComponent.meshletCullingIndirectCommandBuffer = pMeshletFrustrumCullingDynamicResource;
	//bufferComponent.meshletOcclusionCullingIndirectCommandBuffer = pMeshletOcclusionCullingDynamicResource;
	bufferComponent.meshletCullingResetIndirectCommandBuffer = pMeshletFrustrumCullingResetDynamicResource;

	m_viewIDToBuffers[viewID] = bufferComponent;

	return bufferComponent;
}

// Remove buffers associated with an ID
void IndirectCommandBufferManager::UnregisterBuffers(uint64_t viewID) {
	auto& bufferComponent = m_viewIDToBuffers[viewID];
	DeletionManager::GetInstance().MarkForDelete(bufferComponent.opaqueIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
	DeletionManager::GetInstance().MarkForDelete(bufferComponent.alphaTestIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
	DeletionManager::GetInstance().MarkForDelete(bufferComponent.blendIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
	DeletionManager::GetInstance().MarkForDelete(bufferComponent.meshletCullingIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
	//DeletionManager::GetInstance().MarkForDelete(bufferComponent.meshletOcclusionCullingIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
	DeletionManager::GetInstance().MarkForDelete(bufferComponent.meshletCullingResetIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame

	m_opaqueResourceGroup->RemoveResource(bufferComponent.opaqueIndirectCommandBuffer->GetResource().get());
	m_alphaTestResourceGroup->RemoveResource(bufferComponent.alphaTestIndirectCommandBuffer->GetResource().get());
	m_blendResourceGroup->RemoveResource(bufferComponent.blendIndirectCommandBuffer->GetResource().get());

	m_meshletCullingCommandResourceGroup->RemoveResource(bufferComponent.meshletCullingIndirectCommandBuffer->GetResource().get());
	//m_meshletCullingCommandResourceGroup->RemoveResource(bufferComponent.meshletOcclusionCullingIndirectCommandBuffer->GetResource().get());
	m_meshletCullingCommandResourceGroup->RemoveResource(bufferComponent.meshletCullingResetIndirectCommandBuffer->GetResource().get());

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
		if (newSize > 1000) {
			spdlog::info("Alpha test command buffer size: {}", newSize);
		}
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
			bufferComponent.opaqueIndirectCommandBuffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_opaqueCommandBufferSize, sizeof(DispatchMeshIndirectCommand), true, true));
			break;
		case MaterialBuckets::AlphaTest:
			bufferComponent.alphaTestIndirectCommandBuffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_alphaTestCommandBufferSize, sizeof(DispatchMeshIndirectCommand), true, true));
			break;
		case MaterialBuckets::Blend:
			bufferComponent.blendIndirectCommandBuffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_blendCommandBufferSize, sizeof(DispatchMeshIndirectCommand), true, true));
			break;
		}
		deletionManager.MarkForDelete(bufferComponent.meshletCullingIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
		//deletionManager.MarkForDelete(bufferComponent.meshletOcclusionCullingIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
		deletionManager.MarkForDelete(bufferComponent.meshletCullingResetIndirectCommandBuffer->GetResource()); // Delay deletion until after the current frame
		bufferComponent.meshletCullingIndirectCommandBuffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_totalIndirectCommands, sizeof(DispatchIndirectCommand), true, true));
		//bufferComponent.meshletOcclusionCullingIndirectCommandBuffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_totalIndirectCommands, sizeof(DispatchIndirectCommand), false, true, true));
		bufferComponent.meshletCullingResetIndirectCommandBuffer->SetResource(ResourceManager::GetInstance().CreateIndexedStructuredBuffer(m_totalIndirectCommands, sizeof(DispatchIndirectCommand), true, true));
    }
}

void IndirectCommandBufferManager::SetIncrementSize(unsigned int incrementSize) {
    m_incrementSize = incrementSize;
}

std::shared_ptr<Resource> IndirectCommandBufferManager::ProvideResource(ResourceIdentifier const& key) {
	return m_resources[key];
}

std::vector<ResourceIdentifier> IndirectCommandBufferManager::GetSupportedKeys() {
	std::vector<ResourceIdentifier> keys;
	keys.reserve(m_resources.size());
	for (auto const& [key, _] : m_resources)
		keys.push_back(key);

	return keys;
}