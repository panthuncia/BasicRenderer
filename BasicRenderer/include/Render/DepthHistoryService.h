#pragma once

#include <memory>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "Render/ViewStateArtifacts.h"
#include "Resources/PixelBuffer.h"
#include "Render/PreparedPass.h"

namespace org { class PreparedLifecycleEffect; }

namespace br::render {

class IDepthHistoryService {
public:
    virtual ~IDepthHistoryService() = default;
    virtual std::shared_ptr<const org::PreparedLifecycleEffect>
        ReserveDepthHistoryPublication(
            std::shared_ptr<const PublishedViewFamilyState> views,
            std::uint64_t producerFrameNumber) = 0;
};

// Submission-owned temporal state. ViewManager still produces view resources,
// but accepted frames never read or mutate its notion of "previous frame".
class DepthHistoryPublicationService final : public IDepthHistoryService {
public:
    void Clear() {
        std::lock_guard lock(m_mutex);
        m_published.clear();
    }

    DepthHistorySelection Select(std::uint64_t viewID,
        const std::shared_ptr<org::PixelBuffer>& current) const {
        if (!current) return {};
        std::lock_guard lock(m_mutex);
        const auto it = m_published.find(viewID);
        if (it == m_published.end()) return {};
        const auto& selected = it->second;
        if (!selected.resource ||
            selected.resource->GetGlobalResourceID() != current->GetGlobalResourceID() ||
            selected.resource->GetBackingGeneration() != current->GetBackingGeneration()) {
            return {};
        }
        return selected;
    }

    std::shared_ptr<const org::PreparedLifecycleEffect>
        ReserveDepthHistoryPublication(
            std::shared_ptr<const PublishedViewFamilyState> views,
            std::uint64_t producerFrameNumber) override {
        struct Reservation {
            DepthHistoryPublicationService* service = nullptr;
            std::shared_ptr<const PublishedViewFamilyState> views;
            std::uint64_t producerFrameNumber = 0;
        };
        auto reservation = std::make_shared<Reservation>(Reservation{
            this, std::move(views), producerFrameNumber });
        const auto submitted = [](Reservation& value, org::SubmissionContext context) {
            if (!value.service || !value.views) return;
            std::lock_guard lock(value.service->m_mutex);
            for (const auto& view : value.views->views) {
                if (!view.linearDepthMap) continue;
                value.service->m_published[view.id] = {
                    view.linearDepthMap,
                    view.linearDepthMap->GetBackingGeneration(),
                    context.submissionID,
                    value.producerFrameNumber };
            }
        };
        return std::make_shared<const org::PreparedOwnedLifecycle<Reservation>>(
            std::move(reservation), submitted);
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::uint64_t, DepthHistorySelection> m_published;
};

} // namespace br::render
