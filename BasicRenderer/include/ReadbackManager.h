#pragma once
#include <memory>
#include <vector>
#include "SettingsManager.h"

#pragma once

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "Texture.h"
#include "ResourceHandles.h"
#include "SettingsManager.h"
#include "UploadManager.h"
#include "ReadbackRequest.h"
#include "RendererUtils.h"
#include "utilities.h"

struct ReadbackInfo {
    bool cubemap;
	std::shared_ptr<Texture> texture;
	std::wstring outputFile;
	std::function<void()> callback;
};

class ReadbackManager {
public:
	static ReadbackManager& GetInstance(RendererUtils* m_utils);

	void Initialize(RendererUtils utils) {
		m_readbackPass.Setup();
        m_readbackPass.SetReadbackFence(utils.GetReadbackFence());
	}

	void RequestReadback(std::shared_ptr<Texture> texture, std::wstring outputFile, std::function<void()> callback, bool cubemap) {
		m_queuedReadbacks.push_back({cubemap, texture, outputFile, callback });
	}



private:

    class ReadbackPass : public RenderPass {
    public:
        ReadbackPass(RendererUtils utils) : m_utils(utils) {
        }

        void Setup() override {
            auto& manager = DeviceManager::GetInstance();
            auto& device = manager.GetDevice();
            uint8_t numFramesInFlight = SettingsManager::GetInstance().getSettingGetter<uint8_t>("numFramesInFlight")();
            for (int i = 0; i < numFramesInFlight; i++) {
                ComPtr<ID3D12CommandAllocator> allocator;
                ComPtr<ID3D12GraphicsCommandList> commandList;
                ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
                ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
                commandList->Close();
                m_allocators.push_back(allocator);
                m_commandLists.push_back(commandList);
            }
        }

        RenderPassReturn Execute(RenderContext& context) override {
            auto& psoManager = PSOManager::GetInstance();
            auto& commandList = m_commandLists[context.frameIndex];
            auto& allocator = m_allocators[context.frameIndex];
            ThrowIfFailed(allocator->Reset());
            commandList->Reset(allocator.Get(), nullptr);

			for (auto& readback : ReadbackManager::GetInstance(nullptr).m_queuedReadbacks) {
                UINT64 fenceValue = m_readbackFence->GetCompletedValue() + 1;
				if (readback.cubemap) {
					m_utils.SaveCubemapToDDS(context.device, commandList.Get(), readback.texture.get(), readback.outputFile, fenceValue);
				}
				else {
					m_utils.SaveTextureToDDS(context.device, commandList.Get(), context.commandQueue, readback.texture.get(), readback.outputFile, fenceValue);
				}
            }

            commandList->Close();

            return { { commandList.Get() } };
        }

        void Cleanup(RenderContext& context) override {
            // Cleanup if necessary
        }

		void SetReadbackFence(ID3D12Fence* fence) {
			m_readbackFence = fence;
		}

    private:        
        std::vector<ComPtr<ID3D12GraphicsCommandList>> m_commandLists;
        std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
        RendererUtils m_utils;
        ID3D12Fence* m_readbackFence = nullptr;
    };

    ReadbackManager(RendererUtils* m_utils) :  m_readbackPass(*m_utils) {
    
    }

    std::vector<ReadbackInfo> m_queuedReadbacks;
	ReadbackPass m_readbackPass;

    // Static pointer to hold the instance
    static std::unique_ptr<ReadbackManager> instance;
    // Static initialization flag
    static bool initialized;
};

std::unique_ptr<ReadbackManager> ReadbackManager::instance;
bool ReadbackManager::initialized = false;

inline ReadbackManager& ReadbackManager::GetInstance(RendererUtils* m_utils) {
    if (!initialized) {
        instance = std::unique_ptr<ReadbackManager>(new ReadbackManager(m_utils));
        initialized = true;
    }
    return *instance;
}