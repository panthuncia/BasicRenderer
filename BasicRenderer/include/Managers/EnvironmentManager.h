#pragma once

#include <memory>
#include <optional>
#include <mutex>
#include <vector>
#include <functional>
#include <string>

#include "Scene/Environment.h"
#include "Render/Runtime/FrameWorkQueue.h"
#include "Render/EnvironmentWorkService.h"
#include "ShaderBuffers.h"
#include "Resources/Buffers/LazyDynamicStructuredBuffer.h"
#include "Resources/ResourceGroup.h"
#include "Resources/PixelBuffer.h"
#include "Interfaces/IResourceProvider.h"

namespace org { class BufferView; }
using org::BufferView;
namespace org { class DynamicBuffer; }
using org::DynamicBuffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;

class EnvironmentManager : public IResourceProvider {
public:
	using RequestReadbackFn = std::function<void(std::shared_ptr<PixelBuffer>, std::wstring, std::function<void()>, bool)>;

	static std::unique_ptr<EnvironmentManager> CreateUnique() {
		return std::unique_ptr<EnvironmentManager>(new EnvironmentManager());
	}

	void SetRequestReadbackFn(RequestReadbackFn fn) {
		m_requestReadback = std::move(fn);
	}
	void SetWorkServices(br::render::EnvironmentWorkServices services) {
		m_workServices = std::move(services);
	}

	std::unique_ptr<Environment> CreateEnvironment(std::wstring name = L"");
	void RemoveEnvironment(Environment* environment);
	

	void UpdateEnvironmentView(const Environment& environment) {
		m_environmentInfoBuffer->UpdateView(environment.GetEnvironmentBufferView(), &environment.m_environmentInfo);
	}

	void SetFromHDRI(Environment* e, std::string hdriPath);

	std::shared_ptr<Resource> ProvideResource(ResourceIdentifier const& key) override;
	std::vector<ResourceIdentifier> GetSupportedKeys() override;
	std::vector<ResourceIdentifier> GetSupportedResolverKeys() override;
	std::shared_ptr<IResourceResolver> ProvideResolver(ResourceIdentifier const& key) override;

private:
	EnvironmentManager();
	std::unordered_map<ResourceIdentifier, std::shared_ptr<Resource>, ResourceIdentifier::Hasher> m_resources;
	std::unordered_map<ResourceIdentifier, std::shared_ptr<IResourceResolver>, ResourceIdentifier::Hasher> m_resolvers;

	std::shared_ptr<LazyDynamicStructuredBuffer<EnvironmentInfo>> m_environmentInfoBuffer;
	std::mutex m_environmentInfoBufferMutex; // Mutex for thread safety

	unsigned int m_skyboxResolution = 2048;
	unsigned int m_reflectionCubemapResolution = 512;

	// Queue ownership belongs to the renderer-scoped service. The environment
	// artifact producer only submits owned work requests into that service.
	br::render::EnvironmentWorkServices m_workServices;
	std::shared_ptr<std::mutex> m_environmentUpdateMutex = std::make_shared<std::mutex>(); // Mutex for thread safety

	std::shared_ptr<ResourceGroup> m_workingEnvironmentCubemapGroup; // Temporary group for prefiltered cubemap generation
	std::shared_ptr<ResourceGroup> m_workingHDRIGroup; // Temporary group for prefiltered cubemap generation

	std::shared_ptr<ResourceGroup> m_environmentPrefilteredCubemapGroup;
	RequestReadbackFn m_requestReadback;

	friend class Environment;
};
