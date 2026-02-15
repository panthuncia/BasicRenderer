#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <rhi.h>
#include "Managers/Singletons/SettingsManager.h"

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Resources/ReadbackRequest.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"

struct ReadbackInfo {
    bool cubemap;
	std::shared_ptr<PixelBuffer> texture;
	std::wstring outputFile;
	std::function<void()> callback;
};

struct ReadbackCaptureInfo {
    std::string passName;
    std::weak_ptr<Resource> resource;
    RangeSpec range{};
    ReadbackCaptureCallback callback;
};

struct ReadbackCaptureToken {
    uint64_t id = 0;
};

class ReadbackManager {
public:
	static ReadbackManager& GetInstance();

	void Initialize(rhi::Timeline readbackFence) {
		m_readbackPass->Setup();
		m_readbackFence = readbackFence;
        m_readbackPass->SetReadbackFence(readbackFence);
	}

	void RequestReadback(std::shared_ptr<PixelBuffer> texture, std::wstring outputFile, std::function<void()> callback, bool cubemap) {
		m_queuedReadbacks.push_back({cubemap, texture, outputFile, callback });
	}

    void RequestReadbackCapture(
        const std::string& passName,
        Resource* resource,
        const RangeSpec& range,
        ReadbackCaptureCallback callback);

    std::vector<ReadbackCaptureInfo> ConsumeCaptureRequests();

    ReadbackCaptureToken EnqueueCapture(ReadbackCaptureRequest&& request);
    void FinalizeCapture(ReadbackCaptureToken token, uint64_t fenceValue);

    uint64_t GetNextReadbackFenceValue();
    rhi::Timeline GetReadbackFence() const { return m_readbackFence; }

	std::shared_ptr<RenderPass> GetReadbackPass() {
		return m_readbackPass;
	}

    void ClearReadbacks() {
        m_queuedReadbacks.clear();
    }

    void ProcessReadbackRequests();

    void Cleanup() {
        m_queuedReadbacks.clear();
        m_readbackPass.reset();
        m_queuedCaptures.clear();
        m_readbackCaptureRequests.clear();
    }

private:

    class ReadbackPass : public RenderPass {
    public:
        ReadbackPass() {
        }

        void Setup() override {
            
        }

        void ExecuteImmediate(ImmediateContext& context) override {
            auto& readbackManager = ReadbackManager::GetInstance();
            auto& readbacks = readbackManager.m_queuedReadbacks;
            if (readbacks.empty()) {
                return;
            }
            auto& commandList = context.list;
            for (auto& readback : readbacks) {
                if (readback.cubemap) {
                    readbackManager.SaveCubemapToDDS(context.device, commandList, readback.texture, readback.outputFile, m_fenceValue);
                }
                else {
                    readbackManager.SaveTextureToDDS(context.device, commandList, readback.texture.get(), readback.outputFile, m_fenceValue);
                }
            }
		}

        PassReturn Execute(RenderContext& context) override {
            auto& readbackManager = ReadbackManager::GetInstance();
            auto& readbacks = readbackManager.m_queuedReadbacks;
            if (readbacks.empty()) {
                return { {} };
            }
            m_fenceValue++;
            readbackManager.ClearReadbacks();
            return { m_readbackFence, m_fenceValue };
        }

        void Cleanup() override {
            // Cleanup if necessary
        }

		void SetReadbackFence(rhi::Timeline fence) {
			m_readbackFence = fence;
		}

    private:
        rhi::Timeline m_readbackFence;
		UINT64 m_fenceValue = 0;
    };

    ReadbackManager() {
		m_readbackPass = std::make_shared<ReadbackPass>();
    }

    void SaveCubemapToDDS(rhi::Device& device, rg::imm::ImmediateCommandList& commandList, std::shared_ptr<PixelBuffer> cubemap, const std::wstring& outputFile, UINT64 fenceValue);
    void SaveTextureToDDS(
        rhi::Device& device,
        rg::imm::ImmediateCommandList& commandList,
        PixelBuffer* texture,
        const std::wstring& outputFile,
        uint64_t fenceValue);
    std::vector<ReadbackInfo> m_queuedReadbacks;
	std::shared_ptr<ReadbackPass> m_readbackPass;
    rhi::Timeline m_readbackFence;
    std::mutex readbackRequestsMutex;
    std::vector<ReadbackRequest> m_readbackRequests;
    std::vector<ReadbackCaptureRequest> m_readbackCaptureRequests;

    std::mutex m_captureQueueMutex;
    std::vector<ReadbackCaptureInfo> m_queuedCaptures;
    std::atomic<uint64_t> m_captureTokenCounter = 0;
    uint64_t m_captureFenceValue = 0;

    // Static pointer to hold the instance
    static std::unique_ptr<ReadbackManager> instance;
    // Static initialization flag
    static bool initialized;
};

inline ReadbackManager& ReadbackManager::GetInstance() {
    if (!initialized) {
        instance = std::unique_ptr<ReadbackManager>(new ReadbackManager());
        initialized = true;
    }
    return *instance;
}