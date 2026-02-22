#include "Managers/Singletons/ReadbackManager.h"

#include <cstring>

std::unique_ptr<ReadbackManager> ReadbackManager::instance = nullptr;
bool ReadbackManager::initialized = false;

void ReadbackManager::RequestReadbackCapture(
    const std::string& passName,
    Resource* resource,
    const RangeSpec& range,
    ReadbackCaptureCallback callback)
{
    std::weak_ptr<Resource> weakResource;
    if (resource) {
        weakResource = resource->weak_from_this();
    }

    std::scoped_lock lock(m_captureQueueMutex);
    m_queuedCaptures.push_back(ReadbackCaptureInfo{
        passName,
        weakResource,
        range,
        std::move(callback)
        });
}

std::vector<ReadbackCaptureInfo> ReadbackManager::ConsumeCaptureRequests() {
    std::lock_guard<std::mutex> lock(m_captureQueueMutex);
    auto out = std::move(m_queuedCaptures);
    m_queuedCaptures.clear();
    return out;
}

ReadbackCaptureToken ReadbackManager::EnqueueCapture(ReadbackCaptureRequest&& request) {
    request.token = ++m_captureTokenCounter;
    std::lock_guard<std::mutex> lock(readbackRequestsMutex);
    m_readbackCaptureRequests.push_back(std::move(request));
    return { request.token };
}

void ReadbackManager::FinalizeCapture(ReadbackCaptureToken token, uint64_t fenceValue) {
    std::lock_guard<std::mutex> lock(readbackRequestsMutex);
    for (auto& request : m_readbackCaptureRequests) {
        if (request.token == token.id) {
            request.fenceValue = fenceValue;
            return;
        }
    }
}

uint64_t ReadbackManager::GetNextReadbackFenceValue() {
    return ++m_captureFenceValue;
}

void ReadbackManager::ProcessReadbackRequests() {
    std::lock_guard<std::mutex> lock(readbackRequestsMutex);
    const auto completedValue = m_readbackFence.GetCompletedValue();

    std::vector<ReadbackCaptureRequest> remainingCaptures;
    for (auto& request : m_readbackCaptureRequests) {
        if (request.fenceValue != 0 && completedValue >= request.fenceValue) {
            void* mappedData = nullptr;
            request.readbackBuffer->GetAPIResource().Map(&mappedData);

            ReadbackCaptureResult result{};
            result.desc = request.desc;
            result.layouts = request.layouts;
            result.format = request.format;
            result.width = request.width;
            result.height = request.height;
            result.depth = request.depth;
            result.data.resize(request.totalSize);

            std::memcpy(result.data.data(), mappedData, request.totalSize);
            request.readbackBuffer->GetAPIResource().Unmap(0, 0);

            if (request.callback) {
                request.callback(std::move(result));
            }
        }
        else {
            remainingCaptures.push_back(std::move(request));
        }
    }

    m_readbackCaptureRequests = std::move(remainingCaptures);
}