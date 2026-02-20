#include "Managers/Singletons/ResourceManager.h"

#include <rhi_helpers.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/UploadManager.h"
void ResourceManager::Initialize() {

	auto device = DeviceManager::GetInstance().GetDevice();
	DescriptorHeapManager::GetInstance().Initialize();

	m_perFrameBuffer = CreateIndexedConstantBuffer(sizeof(PerFrameCB), "PerFrameCB");

	perFrameCBData.ambientLighting = DirectX::XMVectorSet(0.1f, 0.1f, 0.1f, 1.0f);
	perFrameCBData.numShadowCascades = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numDirectionalLightCascades")();
	auto shadowCascadeSplits = SettingsManager::GetInstance().getSettingGetter<std::vector<float>>("directionalLightCascadeSplits")();
	switch (perFrameCBData.numShadowCascades) {
	case 1:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], 0, 0, 0);
		break;
	case 2:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], 0, 0);
		break;
	case 3:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], shadowCascadeSplits[2], 0);
		break;
	case 4:
		perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(shadowCascadeSplits[0], shadowCascadeSplits[1], shadowCascadeSplits[2], shadowCascadeSplits[3]);
	}

	auto result = device.CreateCommittedResource(rhi::helpers::ResourceDesc::Buffer(sizeof(UINT), rhi::HeapType::Upload), m_uavCounterReset);

	void* pMappedCounterReset = nullptr;
	
    m_uavCounterReset->Map(&pMappedCounterReset, 0, 0);
	ZeroMemory(pMappedCounterReset, sizeof(UINT));
	m_uavCounterReset->Unmap(0, 0);
}

rhi::DescriptorHeap ResourceManager::GetSRVDescriptorHeap() {
    return DescriptorHeapManager::GetInstance().GetSRVDescriptorHeap();
}

rhi::DescriptorHeap ResourceManager::GetSamplerDescriptorHeap() {
    return DescriptorHeapManager::GetInstance().GetSamplerDescriptorHeap();
}


void ResourceManager::UpdatePerFrameBuffer(UINT cameraIndex, UINT numLights, DirectX::XMUINT2 screenRes, DirectX::XMUINT3 clusterSizes, unsigned int frameIndex) {
	perFrameCBData.mainCameraIndex = cameraIndex;
	perFrameCBData.numLights = numLights;
	perFrameCBData.screenResX = screenRes.x;
	perFrameCBData.screenResY = screenRes.y;
	perFrameCBData.lightClusterGridSizeX = clusterSizes.x;
	perFrameCBData.lightClusterGridSizeY = clusterSizes.y;
	perFrameCBData.lightClusterGridSizeZ = clusterSizes.z;
	perFrameCBData.nearClusterCount = 4;
	perFrameCBData.clusterZSplitDepth = 6.0f;
	perFrameCBData.frameIndex = frameIndex % 64; // Wrap around every 64 frames

	BUFFER_UPLOAD(&perFrameCBData, sizeof(PerFrameCB), UploadManager::UploadTarget::FromShared(m_perFrameBuffer), 0);
}

UINT ResourceManager::CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc) {
    return DescriptorHeapManager::GetInstance().CreateIndexedSampler(samplerDesc);
}

void ResourceManager::AssignDescriptorSlots(
    GloballyIndexedResource& target,
    rhi::Resource& apiResource,
    const ViewRequirements& req)
{
    DescriptorHeapManager::GetInstance().AssignDescriptorSlots(target, apiResource, req);
}

void ResourceManager::ReserveDescriptorSlots(
    GloballyIndexedResource& target,
    const ViewRequirements& req)
{
    DescriptorHeapManager::GetInstance().ReserveDescriptorSlots(target, req);
}

void ResourceManager::UpdateDescriptorContents(
    GloballyIndexedResource& target,
    rhi::Resource& apiResource,
    const ViewRequirements& req)
{
    DescriptorHeapManager::GetInstance().UpdateDescriptorContents(target, apiResource, req);
}

void ResourceManager::Cleanup()
{
	m_perFrameBuffer.reset();
    DescriptorHeapManager::GetInstance().Cleanup();
	m_uavCounterReset.Reset();
}