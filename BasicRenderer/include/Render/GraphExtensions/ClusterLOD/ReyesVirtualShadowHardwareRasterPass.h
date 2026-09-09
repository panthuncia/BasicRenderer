#pragma once

#include <memory>
#include <vector>

#include <rhi.h>

#include "Interfaces/IDynamicDeclaredResources.h"
#include "Render/GraphExtensions/ClusterLOD/CLodCommon.h"
#include "Render/PipelineState.h"
#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include "RenderPasses/PreparedComputeDispatch.h"

namespace org { class Buffer; }
using org::Buffer;
namespace org { class PixelBuffer; }
using org::PixelBuffer;
namespace org { class ResourceGroup; }
using org::ResourceGroup;

struct ReyesShadowHardwareFrameData {
    struct Bucket {
        org::PreparedProgramReference program;
        std::vector<unsigned int> descriptorIndices;
        uint64_t argumentsOffset = 0;
    };
    rhi::DescriptorHeapHandle resourceHeap{}, samplerHeap{};
    rhi::CommandSignatureHandle signature{};
    org::PreparedResourceReference arguments;
    std::array<unsigned int, NumMiscUintRootConstants> constants{};
    std::vector<Bucket> buckets;
    uint32_t width = 1, height = 1;
};

class ReyesVirtualShadowHardwareRasterPass final : public org::TypedRenderGraphPass<ReyesVirtualShadowHardwareRasterPass, ReyesShadowHardwareFrameData>, public IDynamicDeclaredResources {
public:
    ReyesVirtualShadowHardwareRasterPass(
        std::shared_ptr<Buffer> visibleClustersBuffer,
        std::shared_ptr<Buffer> rasterBucketsHistogramBuffer,
        std::shared_ptr<Buffer> rasterBucketsIndirectArgsBuffer,
        std::shared_ptr<Buffer> packedRasterWorkGroupsBuffer,
        std::shared_ptr<Buffer> compactedRasterWorkIndicesBuffer,
        std::shared_ptr<Buffer> rasterWorkBuffer,
        std::shared_ptr<Buffer> diceQueueBuffer,
        std::shared_ptr<Buffer> tessTableConfigsBuffer,
        std::shared_ptr<Buffer> tessTableVerticesBuffer,
        std::shared_ptr<Buffer> tessTableTrianglesBuffer,
        std::shared_ptr<PixelBuffer> virtualShadowPageTableTexture,
        std::shared_ptr<PixelBuffer> virtualShadowPhysicalPagesTexture,
        std::shared_ptr<PixelBuffer> virtualShadowDynamicPagesTexture,
        std::shared_ptr<Buffer> virtualShadowClipmapInfoBuffer,
        std::shared_ptr<Buffer> telemetryBuffer,
        std::shared_ptr<ResourceGroup> slabResourceGroup);
    ~ReyesVirtualShadowHardwareRasterPass();

    void Declare(org::PassBuilder& builder);
    void Update(const UpdateExecutionContext& executionContext) override;
    bool DeclaredResourcesChanged() const override;
    ReyesShadowHardwareFrameData Prepare(const org::PassPrepareContext& preparation);
    static void Record(const ReyesShadowHardwareFrameData& data, org::PassRecordContext& recording);

private:
    std::shared_ptr<Buffer> m_visibleClustersBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsHistogramBuffer;
    std::shared_ptr<Buffer> m_rasterBucketsIndirectArgsBuffer;
    std::shared_ptr<Buffer> m_packedRasterWorkGroupsBuffer;
    std::shared_ptr<Buffer> m_compactedRasterWorkIndicesBuffer;
    std::shared_ptr<Buffer> m_rasterWorkBuffer;
    std::shared_ptr<Buffer> m_diceQueueBuffer;
    std::shared_ptr<Buffer> m_tessTableConfigsBuffer;
    std::shared_ptr<Buffer> m_tessTableVerticesBuffer;
    std::shared_ptr<Buffer> m_tessTableTrianglesBuffer;
    std::shared_ptr<PixelBuffer> m_virtualShadowPageTableTexture;
    std::shared_ptr<PixelBuffer> m_virtualShadowPhysicalPagesTexture;
    std::shared_ptr<PixelBuffer> m_virtualShadowDynamicPagesTexture;
    std::shared_ptr<Buffer> m_virtualShadowClipmapInfoBuffer;
    std::shared_ptr<Buffer> m_telemetryBuffer;
    std::shared_ptr<ResourceGroup> m_slabResourceGroup;
    std::shared_ptr<Buffer> m_viewRasterInfoBuffer;

    std::vector<CLodViewRasterInfo> m_viewRasterInfos;
    std::shared_ptr<rhi::CommandSignaturePtr> m_rasterizationCommandSignature;
    uint32_t m_passWidth = 1u;
    uint32_t m_passHeight = 1u;
    bool m_declaredResourcesChanged = true;
};
