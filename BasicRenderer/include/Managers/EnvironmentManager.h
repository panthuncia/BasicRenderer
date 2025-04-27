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

	std::unique_ptr<Environment> CreateEnvironment();
	void RemoveEnvironment(Environment* environment);
	
	const std::vector<Environment*>& GetEnvironmentsToConvert() {
		return m_environmentsToConvert;
	}

	void UpdateEnvironmentView(const Environment& environment) {
		std::lock_guard<std::mutex> lock(m_environmentUpdateMutex);
		m_environmentInfoBuffer->UpdateView(environment.GetEnvironmentBufferView(), &environment.m_environmentInfo);
	}

	void ClearEnvironmentsToConvert() {
		m_environmentsToConvert.clear();
		m_workingHDRIGroup->ClearResources(); // HDRIs not needed after conversion to cubemaps
	}

	const std::vector<Environment*>& GetEnvironmentsToPrefilter() {
		return m_environmentsToPrefilter;
	}

	void ClearEnvironmentsToPrefilter() {
		m_environmentsToPrefilter.clear();
		m_workingEnvironmentCubemapGroup->ClearResources(); // Full-res cubemaps not needed after prefiltering
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

private:
	EnvironmentManager();
	std::shared_ptr<LazyDynamicStructuredBuffer<EnvironmentInfo>> m_environmentInfoBuffer;
	std::mutex m_environmentInfoBufferMutex; // Mutex for thread safety

	unsigned int m_skyboxResolution = 2048;
	unsigned int m_reflectionCubemapResolution = 512;

	std::vector<Environment*> m_environmentsToConvert;
	std::vector<Environment*> m_environmentsToPrefilter;
	std::mutex m_environmentUpdateMutex; // Mutex for thread safety

	std::shared_ptr<ResourceGroup> m_workingEnvironmentCubemapGroup; // Temporary group for prefiltered cubemap generation
	std::shared_ptr<ResourceGroup> m_workingHDRIGroup; // Temporary group for prefiltered cubemap generation

	std::shared_ptr<ResourceGroup> m_environmentPrefilteredCubemapGroup;

	void SetFromHDRI(Environment* e, std::shared_ptr<Texture>& HDRI);

	friend class Environment;
};