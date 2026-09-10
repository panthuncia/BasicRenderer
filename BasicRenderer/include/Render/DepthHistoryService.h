#pragma once

#include <memory>

namespace org { class PreparedLifecycleEffect; }

namespace br::render {

class IDepthHistoryService {
public:
    virtual ~IDepthHistoryService() = default;
    virtual std::shared_ptr<const org::PreparedLifecycleEffect>
        ReserveDepthHistoryPublication() = 0;
};

} // namespace br::render
