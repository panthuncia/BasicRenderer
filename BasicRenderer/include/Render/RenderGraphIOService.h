#pragma once

#include "Render/RenderGraph/RenderGraph.h"

#include <vector>

class TextureFactory;
class ITextureStreamingFeedbackService;
namespace br { class ReadbackManager; }
namespace org::runtime { class IUploadService; }

namespace br::render {

// Device-scoped owner for operations imported into each graph generation.
class RenderGraphIOService final {
public:
    RenderGraphIOService(TextureFactory&, org::runtime::IUploadService&,
        br::ReadbackManager&, ITextureStreamingFeedbackService&) noexcept;

    void SetRegistry(org::ResourceRegistry&);
    void GatherStructuralPasses(std::vector<RenderGraph::ExternalPassDesc>&);
    void GatherFramePasses(std::vector<RenderGraph::ExternalPassDesc>&);

private:
    TextureFactory* m_textures;
    org::runtime::IUploadService* m_uploads;
    br::ReadbackManager* m_readbacks;
    ITextureStreamingFeedbackService* m_textureStreamingFeedback;
};

} // namespace br::render
