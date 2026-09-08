#pragma once

#include "RenderPasses/Base/TypedRenderGraphPass.h"
#include <vector>

namespace org { class PixelBuffer; }
using org::PixelBuffer;

struct PerViewLinearDepthCopyPreparedView {
    std::vector<uint32_t> constants;
    uint32_t groupsX = 0, groupsY = 0;
};
struct PerViewLinearDepthCopyPreparedData {
    rhi::DescriptorHeapHandle resourceHeap{}, samplerHeap{};
    rhi::PipelineLayoutHandle layout{};
    org::PreparedProgramReference program{};
    std::vector<PerViewLinearDepthCopyPreparedView> views;
};

class PerViewLinearDepthCopyPass : public org::TypedRenderGraphPass<PerViewLinearDepthCopyPass, PerViewLinearDepthCopyPreparedData> {
public:
    explicit PerViewLinearDepthCopyPass(bool writeProjectedDepth = true);

    void Declare(org::PassBuilder& builder);
    void Initialize();
    PerViewLinearDepthCopyPreparedData Prepare(const org::PassPrepareContext& preparation);
    static void Record(const PerViewLinearDepthCopyPreparedData&, org::PassRecordContext&);

private:
    using PreparedView = PerViewLinearDepthCopyPreparedView;
    using PreparedData = PerViewLinearDepthCopyPreparedData;
    PipelineState m_pso;
    PixelBuffer* m_pProjectedDepthTexture = nullptr;
    PixelBuffer* m_pCanonicalDeviceDepth = nullptr;
    bool m_writeProjectedDepth = true;
};
