#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Render/AsyncStateGraph.h"
#include "Scene/Components.h"
#include <DirectXMath.h>

namespace org { class Resource; }

namespace br::render {

struct PublishedDirectionalShadowLight {
    DirectX::XMFLOAT3 direction{};
    std::vector<std::uint64_t> viewIDs;
    std::vector<std::int64_t> unwrappedPageOffsetX;
    std::vector<std::int64_t> unwrappedPageOffsetY;
};

struct LightTableBuildInput {
    std::uint64_t revision = 0;
    std::uint32_t lightCount = 0;
    std::uint32_t lightPagePoolSize = 0;
    std::vector<PublishedDirectionalShadowLight> directionalShadows;
    std::vector<std::shared_ptr<org::Resource>> retainedResources;
};

struct PublishedLightTableState {
    std::uint64_t revision = 0;
    std::uint32_t lightCount = 0;
    std::uint32_t lightPagePoolSize = 0;
    std::uint64_t viewFamilyRevision = 0;
    std::vector<PublishedDirectionalShadowLight> directionalShadows;
    std::vector<std::shared_ptr<org::Resource>> retainedResources;
};

void RegisterLightStateProducer(AsyncStateGraph& graph);

} // namespace br::render
