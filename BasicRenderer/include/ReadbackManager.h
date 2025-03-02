#pragma once
#include <memory>
#include <vector>
#include "SettingsManager.h"

#include "RenderPass.h"
#include "PSOManager.h"
#include "RenderContext.h"
#include "Texture.h"
#include "ResourceHandles.h"
#include "SettingsManager.h"
#include "UploadManager.h"
#include "ReadbackRequest.h"
#include "utilities.h"

struct ReadbackInfo {
    bool cubemap;
	std::shared_ptr<Texture> texture;
	std::wstring outputFile;
	std::function<void()> callback;
};

class ReadbackManager {
public:
	static ReadbackManager& GetInstance();

	void Initialize(ID3D12Fence* readbackFence) {
		m_readbackPass->Setup();
		m_readbackFence = readbackFence;
        m_readbackPass->SetReadbackFence(readbackFence);
	}

	void RequestReadback(std::shared_ptr<Texture> texture, std::wstring outputFile, std::function<void()> callback, bool cubemap) {
		m_queuedReadbacks.push_back({cubemap, texture, outputFile, callback });
	}

	std::shared_ptr<RenderPass> GetReadbackPass() {
		return m_readbackPass;
	}

    void ClearReadbacks() {
        m_queuedReadbacks.clear();
    }

    void ProcessReadbackRequests();

private:

    class ReadbackPass : public RenderPass {
    public:
        ReadbackPass() {
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
            auto& readbackManager = ReadbackManager::GetInstance();
            auto& readbacks = readbackManager.m_queuedReadbacks;
			if (readbacks.empty()) {
				return { {} };
			}
            auto& psoManager = PSOManager::GetInstance();
            auto& commandList = m_commandLists[context.frameIndex];
            auto& allocator = m_allocators[context.frameIndex];
            ThrowIfFailed(allocator->Reset());
            commandList->Reset(allocator.Get(), nullptr);
            m_fenceValue++;
			for (auto& readback : readbacks) {
				if (readback.cubemap) {
                    readbackManager.SaveCubemapToDDS(context.device, commandList.Get(), readback.texture.get(), readback.outputFile, m_fenceValue);
				}
				else {
                    readbackManager.SaveTextureToDDS(context.device, commandList.Get(), context.commandQueue, readback.texture.get(), readback.outputFile, m_fenceValue);
				}
            }

            commandList->Close();

			readbackManager.ClearReadbacks();
			RenderPassReturn passReturn;
			passReturn.commandLists = { commandList.Get() };
			passReturn.fence = m_readbackFence;
			passReturn.fenceValue = m_fenceValue;

            return passReturn;
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
        ID3D12Fence* m_readbackFence = nullptr;
		UINT64 m_fenceValue = 0;
    };

    ReadbackManager() {
		m_readbackPass = std::make_shared<ReadbackPass>();
    }

    void SaveCubemapToDDS(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, Texture* cubemap, const std::wstring& outputFile, UINT64 fenceValue);
    void SaveTextureToDDS(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, ID3D12CommandQueue* commandQueue, Texture* texture, const std::wstring& outputFile, UINT64 fenceValue);

    std::vector<ReadbackInfo> m_queuedReadbacks;
	std::shared_ptr<ReadbackPass> m_readbackPass;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_readbackFence;
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