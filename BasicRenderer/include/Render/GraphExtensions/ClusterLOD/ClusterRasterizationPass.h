#pragma once

#include <memory>
#include <vector>

#include <boost/container_hash/hash.hpp>
#include <rhi.h>

#include "BuiltinRenderPasses.h"
#include "Interfaces/IDynamicDeclaredResources.h"
#include "Render/RenderPhase.h"
#include "RenderPasses/Base/RenderPass.h"
#include "Render/RenderGraph/RenderGraph.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Resources/PixelBuffer.h"

class Buffer;
class ResourceGroup;

struct ClusterRasterizationPassInputs {
    bool wireframe;
    bool clearGbuffer;

    friend bool operator==(const ClusterRasterizationPassInputs&, const ClusterRasterizationPassInputs&) = default;
};

inline rg::Hash64 HashValue(const ClusterRasterizationPassInputs& i) {
    std::size_t seed = 0;

    boost::hash_combine(seed, i.wireframe);
    boost::hash_combine(seed, i.clearGbuffer);
    return seed;
}

class ClusterRasterizationPass : public RenderPass, public IDynamicDeclaredResources {
public:
    ClusterRasterizationPass(
        ClusterRasterizationPassInputs inputs,
        std::shared_ptr<Buffer> compactedVisibleClustersBuffer,
        std::shared_ptr<Buffer> rasterBucketsHistogramBuffer,
        std::shared_ptr<Buffer> rasterBucketsIndirectArgsBuffer,
        std::shared_ptr<ResourceGroup> slabResourceGroup = nullptr);
    ~ClusterRasterizationPass();

    void DeclareResourceUsages(RenderPassBuilder* builder) override;
    void Setup() override;
    void Update(const UpdateExecutionContext& executionContext) override;
    bool DeclaredResourcesChanged() const override;
    PassReturn Execute(PassExecutionContext& executionContext) override;
    void Cleanup() override;

private:
    bool m_wireframe = false;
    bool m_meshShaders = false;
    bool m_clearGbuffer = true;

    std::vector<CLodViewRasterInfo> m_viewRasterInfos;
    std::vector<std::shared_ptr<PixelBuffer>> m_visibilityBuffers;

    std::shared_ptr<Buffer> m_compactedVisibleClustersBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsIndirectArgsBuffer;

    std::shared_ptr<ResourceGroup> m_slabResourceGroup;

    rhi::CommandSignaturePtr m_rasterizationCommandSignature;

    std::shared_ptr<Buffer> m_viewRasterInfoBuffer;
    uint32_t m_passWidth = 1;
    uint32_t m_passHeight = 1;
    bool m_declaredResourcesChanged = true;

    RenderPhase m_renderPhase = Engine::Primary::GBufferPass;
};
