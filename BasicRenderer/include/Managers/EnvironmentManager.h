#pragma once

#include <memory>
#include <optional>
#include <mutex>
#include <vector>
#include <functional>
#include <string>

#include "Scene/Environment.h"
#include "Render/Runtime/FrameWorkQueue.h"
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

	std::unique_ptr<Environment> CreateEnvironment(std::wstring name = L"");
	void RemoveEnvironment(Environment* environment);
	

	void UpdateEnvironmentView(const Environment& environment) {
		m_environmentInfoBuffer->UpdateView(environment.GetEnvironmentBufferView(), &environment.m_environmentInfo);
	}

    struct SHWork {
        std::shared_ptr<PixelBuffer> srcCubemap;
        uint32_t environmentIndex = 0;
        uint32_t cubemapResolution = 0;
    };
    using SHWorkQueue = org::runtime::FrameWorkQueue<SHWork>;
    SHWorkQueue GetSHWorkQueue() const { return m_shWork; }

    struct ConversionWork {
        std::shared_ptr<PixelBuffer> srcTexture, dstCubemap;
        uint32_t environmentIndex = 0;
        std::shared_ptr<ResourceGroup> sourceGroup;
        std::shared_ptr<std::mutex> publicationMutex;
        void Commit() const { std::lock_guard lock(*publicationMutex); sourceGroup->RemoveResource(srcTexture.get()); }
        void Discard() const { Commit(); }
    };
    struct PrefilterWork {
        std::shared_ptr<PixelBuffer> srcCubemap, dstPrefilteredCubemap;
        uint32_t baseResolution = 0, environmentIndex = 0;
        std::shared_ptr<ResourceGroup> sourceGroup;
        std::shared_ptr<std::mutex> publicationMutex;
        void Commit() const { std::lock_guard lock(*publicationMutex); sourceGroup->RemoveResource(srcCubemap.get()); }
        void Discard() const { Commit(); }
    };
    using ConversionWorkQueue = org::runtime::FrameWorkQueue<ConversionWork>;
    using PrefilterWorkQueue = org::runtime::FrameWorkQueue<PrefilterWork>;
    ConversionWorkQueue GetConversionWorkQueue() const { return m_conversionWork; }
    PrefilterWorkQueue GetPrefilterWorkQueue() const { return m_prefilterWork; }

    void PublishWorkTelemetry() const;

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

    ConversionWorkQueue m_conversionWork;
    PrefilterWorkQueue m_prefilterWork;
    SHWorkQueue m_shWork;
	std::shared_ptr<std::mutex> m_environmentUpdateMutex = std::make_shared<std::mutex>(); // Mutex for thread safety

	std::shared_ptr<ResourceGroup> m_workingEnvironmentCubemapGroup; // Temporary group for prefiltered cubemap generation
	std::shared_ptr<ResourceGroup> m_workingHDRIGroup; // Temporary group for prefiltered cubemap generation

	std::shared_ptr<ResourceGroup> m_environmentPrefilteredCubemapGroup;
	RequestReadbackFn m_requestReadback;

	friend class Environment;
};