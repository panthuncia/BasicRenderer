#include "Managers/EnvironmentManager.h"

#include <filesystem>

#include "Managers/Singletons/ResourceManager.h"
#include "Scene/Environment.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Resources/PixelBuffer.h"
#include "Resources/Sampler.h"
#include "Resources/ResourceGroup.h"
#include "Resources/Texture.h"
#include "Managers/Singletons/ReadbackManager.h"

EnvironmentManager::EnvironmentManager() {
	auto& resourceManager = ResourceManager::GetInstance();
	m_skyboxResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("skyboxResolution")();
	m_reflectionCubemapResolution = SettingsManager::GetInstance().getSettingGetter<uint16_t>("reflectionCubemapResolution")();
	m_environmentInfoBuffer = resourceManager.CreateIndexedLazyDynamicStructuredBuffer<EnvironmentInfo>(ResourceState::ALL_SRV, 1, L"environmentsBuffer", 0, true);

	m_workingEnvironmentCubemapGroup = std::make_shared<ResourceGroup>(L"EnvironmentCubemapGroup");
	m_workingHDRIGroup = std::make_shared<ResourceGroup>(L"WorkingHDRIGroup");
	m_environmentPrefilteredCubemapGroup = std::make_shared<ResourceGroup>(L"EnvironmentPrefilteredCubemapGroup");
}

std::unique_ptr<Environment> EnvironmentManager::CreateEnvironment(std::wstring name) {
	auto view = m_environmentInfoBuffer->Add();
	std::unique_ptr<Environment> env = std::make_unique<Environment>(this, name);
	env->SetEnvironmentBufferView(view);

	ImageDimensions dims;
	dims.height = m_reflectionCubemapResolution;
	dims.width = m_reflectionCubemapResolution;
	dims.rowPitch = m_reflectionCubemapResolution * 4;
	dims.slicePitch = m_reflectionCubemapResolution * m_reflectionCubemapResolution * 4;

	TextureDescription prefilteredDesc;
	for (int i = 0; i < 6; i++) {
		prefilteredDesc.imageDimensions.push_back(dims);
	}
	prefilteredDesc.channels = 3;
	prefilteredDesc.isCubemap = true;
	prefilteredDesc.hasRTV = true;
	prefilteredDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	prefilteredDesc.generateMipMaps = true;

	auto prefilteredEnvironmentCubemap = PixelBuffer::Create(prefilteredDesc);
	auto sampler = Sampler::GetDefaultSampler();
	auto prefilteredEnvironment = std::make_shared<Texture>(prefilteredEnvironmentCubemap, sampler);
	prefilteredEnvironment->SetName(L"Environment prefiltered cubemap");

	env->SetEnvironmentPrefilteredCubemap(prefilteredEnvironment);
	env->SetReflectionCubemapResolution(m_reflectionCubemapResolution);

	m_environmentPrefilteredCubemapGroup->AddResource(prefilteredEnvironment);

	return std::move(env);
}

void EnvironmentManager::SetFromHDRI(Environment* e, std::string hdriPath) {

	// Check if this environment has been processed and cached. If it has, load the cache. If it hasn't, load the environment and process it.
	auto& name = e->GetName();
	auto skyboxPath = GetCacheFilePath(name + L"_environment.dds", L"environments");
	std::shared_ptr<Texture> skybox;
	if (std::filesystem::exists(skyboxPath)) {
		skybox = loadCubemapFromFile(skyboxPath, true);
		e->SetReflectionCubemapResolution(skybox->GetBuffer()->GetWidth());
		e->SetEnvironmentCubemap(skybox);
	}
	else {
		auto skyHDR = loadTextureFromFileSTBI(hdriPath);

		TextureDescription skyboxDesc;
		ImageDimensions dims;
		dims.height = m_skyboxResolution;
		dims.width = m_skyboxResolution;
		dims.rowPitch = m_skyboxResolution * 4;
		dims.slicePitch = m_skyboxResolution * m_skyboxResolution * 4;
		for (int i = 0; i < 6; i++) {
			skyboxDesc.imageDimensions.push_back(dims);
		}
		skyboxDesc.channels = 3;
		skyboxDesc.isCubemap = true;
		skyboxDesc.hasRTV = true;
		skyboxDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;

		auto envCubemap = PixelBuffer::Create(skyboxDesc);
		auto sampler = Sampler::GetDefaultSampler();
		skybox = std::make_shared<Texture>(envCubemap, sampler);
		skybox->SetName(L"Environment cubemap");

		e->SetHDRI(skyHDR);
		e->SetEnvironmentCubemap(skybox);
		e->SetReflectionCubemapResolution(m_skyboxResolution); // For HDRI environments, use the same resolution as the skybox

		m_environmentsToConvert.push_back(e);
		auto path = GetCacheFilePath(name+L"_environment.dds", L"environments");
		ReadbackManager::GetInstance().RequestReadback(envCubemap, path, nullptr, true);
	}

	m_environmentsToPrefilter.push_back(e);
	m_workingEnvironmentCubemapGroup->AddResource(skybox);
}

void EnvironmentManager::RemoveEnvironment(Environment* e) {
	std::lock_guard<std::mutex> lock(m_environmentUpdateMutex);
	m_environmentInfoBuffer->Remove(e->GetEnvironmentBufferView());
	m_environmentPrefilteredCubemapGroup->RemoveResource(e->GetEnvironmentPrefilteredCubemap().get());
	m_workingEnvironmentCubemapGroup->RemoveResource(e->GetEnvironmentCubemap().get());
	m_environmentsToConvert.erase(std::remove(m_environmentsToConvert.begin(), m_environmentsToConvert.end(), e), m_environmentsToConvert.end());
	m_environmentsToPrefilter.erase(std::remove(m_environmentsToPrefilter.begin(), m_environmentsToPrefilter.end(), e), m_environmentsToPrefilter.end());
}