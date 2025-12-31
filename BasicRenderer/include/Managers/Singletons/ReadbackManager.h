#pragma once
#include <memory>
#include <vector>
#include <rhi.h>
#include "Managers/Singletons/SettingsManager.h"

#include "RenderPasses/Base/RenderPass.h"
#include "Managers/Singletons/PSOManager.h"
#include "Render/RenderContext.h"
#include "Resources/Texture.h"
#include "Managers/Singletons/UploadManager.h"
#include "Resources/ReadbackRequest.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"

struct ReadbackInfo {
    bool cubemap;
	std::shared_ptr<PixelBuffer> texture;
	std::wstring outputFile;
	std::function<void()> callback;
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
    }

private:

    class ReadbackPass : public RenderPass {
    public:
        ReadbackPass() {
        }

        void Setup() override {
            
        }

        PassReturn Execute(RenderContext& context) override {
            auto& readbackManager = ReadbackManager::GetInstance();
            auto& readbacks = readbackManager.m_queuedReadbacks;
			if (readbacks.empty()) {
				return { {} };
			}
            auto& commandList = context.commandList;
            m_fenceValue++;
			for (auto& readback : readbacks) {
				if (readback.cubemap) {
                    readbackManager.SaveCubemapToDDS(context.device, commandList, readback.texture, readback.outputFile, m_fenceValue);
				}
				else {
                    readbackManager.SaveTextureToDDS(context.device, commandList, readback.texture.get(), readback.outputFile, m_fenceValue);
				}
            }
            
			// Clear the readbacks after processing
			readbackManager.ClearReadbacks();

            return { m_readbackFence, m_fenceValue };
        }

        void Cleanup(RenderContext& context) override {
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

    void SaveCubemapToDDS(rhi::Device& device, rhi::CommandList& commandList, std::shared_ptr<PixelBuffer> cubemap, const std::wstring& outputFile, UINT64 fenceValue);
    void SaveTextureToDDS(
        rhi::Device& device,
        rhi::CommandList& commandList,
        PixelBuffer* texture,
        const std::wstring& outputFile,
        uint64_t fenceValue);
    std::vector<ReadbackInfo> m_queuedReadbacks;
	std::shared_ptr<ReadbackPass> m_readbackPass;
    rhi::Timeline m_readbackFence;
    std::mutex readbackRequestsMutex;
    std::vector<ReadbackRequest> m_readbackRequests;

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