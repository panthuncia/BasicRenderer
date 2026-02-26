#include "Managers/Singletons/ResourceManager.h"

#include <rhi_helpers.h>
#include <OpenRenderGraph/OpenRenderGraph.h>

#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Render/Runtime/UploadServiceAccess.h"

void ResourceManager::Initialize() {

	auto device = DeviceManager::GetInstance().GetDevice();

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
	perFrameCBData.frameIndex = frameIndex;

	BUFFER_UPLOAD(&perFrameCBData, sizeof(PerFrameCB), rg::runtime::UploadTarget::FromShared(m_perFrameBuffer), 0);
}
void ResourceManager::Cleanup()
{
	m_perFrameBuffer.reset();
	m_uavCounterReset.Reset();
}