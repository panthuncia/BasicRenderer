#pragma once

#include <memory>
#include <optional>
#include <mutex>
#include <vector>

#include "Scene/Environment.h"
#include "ShaderBuffers.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Resources/ResourceGroup.h"
#include "Interfaces/IResourceProvider.h"

class BufferView;
class DynamicBuffer;

class EnvironmentManager : public IResourceProvider {
public:
	static std::unique_ptr<EnvironmentManager> CreateUnique() {
		return std::unique_ptr<EnvironmentManager>(new EnvironmentManager());
	}

	std::unique_ptr<Environment> CreateEnvironment(std::wstring name = L"");
	void RemoveEnvironment(Environment* environment);
	

	std::vector<Environment*> GetAndClearEnvironmentsToConvert() & {
		std::lock_guard<std::mutex> lock(m_environmentUpdateMutex);
		auto environments = m_environmentsToConvert; // Copy the vector
		m_environmentsToConvert.clear();
		m_workingHDRIGroup->ClearResources(); // HDRIs not needed after conversion to cubemaps
		return environments;
	}

	void UpdateEnvironmentView(const Environment& environment) {
		m_environmentInfoBuffer->UpdateView(environment.GetEnvironmentBufferView(), &environment.m_environmentInfo);
	}

	const std::vector<Environment*>& GetEnvironmentsToPrefilter() {
		return m_environmentsToPrefilter;
	}

	std::vector<Environment*> GetAndClearEnvironmentsToPrefilter() & {
		std::lock_guard<std::mutex> lock(m_environmentUpdateMutex);
		auto environments = m_environmentsToPrefilter; // Copy the vector
		m_environmentsToPrefilter.clear();
		m_workingEnvironmentCubemapGroup->ClearResources(); // Full-res cubemaps not needed after prefiltering
		return environments;
	}

	const std::vector<Environment*>& GetEnvironmentsToComputeSH() {
		return m_environmentsToComputeSH;
	}

	std::vector<Environment*> GetAndClearEnvironmentsToComputeSH()& {
		std::lock_guard<std::mutex> lock(m_environmentUpdateMutex);
		auto environments = m_environmentsToComputeSH; // Copy the vector
		m_environmentsToComputeSH.clear();
		return environments;
	}

	void SetFromHDRI(Environment* e, std::string hdriPath);

	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
	std::vector<ResourceIdentifier> GetSupportedKeys() override;

private:
	EnvironmentManager();
	std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;
	std::shared_ptr<LazyDynamicStructuredBuffer<EnvironmentInfo>> m_environmentInfoBuffer;
	std::mutex m_environmentInfoBufferMutex; // Mutex for thread safety

	unsigned int m_skyboxResolution = 2048;
	unsigned int m_reflectionCubemapResolution = 512;

	std::vector<Environment*> m_environmentsToConvert;
	std::vector<Environment*> m_environmentsToPrefilter;
	std::vector<Environment*> m_environmentsToComputeSH;
	std::mutex m_environmentUpdateMutex; // Mutex for thread safety

	std::shared_ptr<ResourceGroup> m_workingEnvironmentCubemapGroup; // Temporary group for prefiltered cubemap generation
	std::shared_ptr<ResourceGroup> m_workingHDRIGroup; // Temporary group for prefiltered cubemap generation

	std::shared_ptr<ResourceGroup> m_environmentPrefilteredCubemapGroup;

	friend class Environment;
};