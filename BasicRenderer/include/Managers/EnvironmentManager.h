#pragma once

#include <memory>
#include <optional>
#include <mutex>
#include <vector>

#include "Scene/Environment.h"
#include "ShaderBuffers.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Resources/ResourceGroup.h"

class BufferView;
class DynamicBuffer;

class EnvironmentManager {
public:
	static std::unique_ptr<EnvironmentManager> CreateUnique() {
		return std::unique_ptr<EnvironmentManager>(new EnvironmentManager());
	}

	std::unique_ptr<Environment> CreateEnvironment(std::wstring name = L"");
	void RemoveEnvironment(Environment* environment);
	

	std::vector<Environment*> GetAndClearEncironmentsToConvert() & {
		std::lock_guard<std::mutex> lock(m_environmentUpdateMutex);
		auto environments = m_environmentsToConvert; // Copy the vector
		m_environmentsToConvert.clear();
		m_workingHDRIGroup->ClearResources(); // HDRIs not needed after conversion to cubemaps
		return environments;
	}

	void UpdateEnvironmentView(const Environment& environment) {
		std::lock_guard<std::mutex> lock(m_environmentUpdateMutex);
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

	unsigned int GetEnvironmentBufferSRVDescriptorIndex() const {
		return m_environmentInfoBuffer->GetSRVInfo()[0].index;
	}

	unsigned int GetEnvironmentBufferUAVDescriptorIndex() const {
		return m_environmentInfoBuffer->GetUAVShaderVisibleInfo()[0].index;
	}

	std::shared_ptr<ResourceGroup>& GetWorkingEnvironmentCubemapGroup() {
		return m_workingEnvironmentCubemapGroup;
	}

	std::shared_ptr<ResourceGroup>& GetWorkingHDRIGroup() {
		return m_workingHDRIGroup;
	}

	std::shared_ptr<ResourceGroup>& GetEnvironmentPrefilteredCubemapGroup() {
		return m_environmentPrefilteredCubemapGroup;
	}

	std::shared_ptr<LazyDynamicStructuredBuffer<EnvironmentInfo>>& GetEnvironmentInfoBuffer() {
		return m_environmentInfoBuffer;
	}

	void SetFromHDRI(Environment* e, std::string hdriPath);

private:
	EnvironmentManager();
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