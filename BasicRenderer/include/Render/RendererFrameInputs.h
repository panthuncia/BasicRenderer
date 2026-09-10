#pragma once

#include "Render/PassExecutionContext.h"
#include "Render/RenderContext.h"

#include <memory>
#include <typeindex>

namespace br::render {

// Owned host publication for one accepted logical frame. The top-level context
// values are immutable and never alias Renderer::m_context, but their manager
// pointers remain transitional mutable dependencies. Replace those pointers
// with exact publication leases and service requests before worker preparation.
// The graph request and every raw IHostExecutionData view retain this owner.
class RendererFrameInputs final : public org::IHostExecutionData {
public:
    RendererFrameInputs(
        std::shared_ptr<const UpdateContext> update,
        std::shared_ptr<const RenderContext> render) noexcept
        : m_update(std::move(update)), m_render(std::move(render)) {}

    const void* TryGet(std::type_index type) const noexcept override {
        if (type == std::type_index(typeid(UpdateContext))) return m_update.get();
        if (type == std::type_index(typeid(RenderContext))) return m_render.get();
        if (type == std::type_index(typeid(RendererFrameInputs))) return this;
        return nullptr;
    }

    const std::shared_ptr<const UpdateContext>& Update() const noexcept { return m_update; }
    const std::shared_ptr<const RenderContext>& Render() const noexcept { return m_render; }

private:
    std::shared_ptr<const UpdateContext> m_update;
    std::shared_ptr<const RenderContext> m_render;
};

} // namespace br::render
