#pragma once

#include <memory>
#include <variant>
#include <vector>

#include <rhi.h>

#include "Render/DescriptorHeap.h"

class GloballyIndexedResource;

class DescriptorHeapManager {
public:
    struct ViewRequirements {
        struct TextureViews {
            uint32_t mipLevels = 1;
            bool isCubemap = false;
            bool isArray = false;
            uint32_t arraySize = 1;
            uint32_t totalArraySlices = 1;

            rhi::Format baseFormat = rhi::Format::Unknown;
            rhi::Format srvFormat = rhi::Format::Unknown;
            rhi::Format uavFormat = rhi::Format::Unknown;
            rhi::Format rtvFormat = rhi::Format::Unknown;
            rhi::Format dsvFormat = rhi::Format::Unknown;

            bool createSRV = true;
            bool createUAV = false;
            bool createNonShaderVisibleUAV = false;
            bool createRTV = false;
            bool createDSV = false;

            bool createCubemapAsArraySRV = false;
            uint32_t uavFirstMip = 0;
        };

        struct BufferViews {
            bool createCBV = false;
            bool createSRV = false;
            bool createUAV = false;
            bool createNonShaderVisibleUAV = false;

            rhi::CbvDesc cbvDesc{};
            rhi::SrvDesc srvDesc{};
            rhi::UavDesc uavDesc{};

            uint64_t uavCounterOffset = 0;
        };

        std::variant<TextureViews, BufferViews> views;
    };

    static DescriptorHeapManager& GetInstance() {
        static DescriptorHeapManager instance;
        return instance;
    }

    void Initialize();
    void Cleanup();

    void AssignDescriptorSlots(
        GloballyIndexedResource& target,
        rhi::Resource& apiResource,
        const ViewRequirements& req);

    void ReserveDescriptorSlots(
        GloballyIndexedResource& target,
        const ViewRequirements& req);

    void UpdateDescriptorContents(
        GloballyIndexedResource& target,
        rhi::Resource& apiResource,
        const ViewRequirements& req);

    rhi::DescriptorHeap GetSRVDescriptorHeap() const;
    rhi::DescriptorHeap GetSamplerDescriptorHeap() const;
    UINT CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc);

    const std::shared_ptr<DescriptorHeap>& GetCBVSRVUAVHeap() const { return m_cbvSrvUavHeap; }
    const std::shared_ptr<DescriptorHeap>& GetSamplerHeap() const { return m_samplerHeap; }
    const std::shared_ptr<DescriptorHeap>& GetRTVHeap() const { return m_rtvHeap; }
    const std::shared_ptr<DescriptorHeap>& GetDSVHeap() const { return m_dsvHeap; }
    const std::shared_ptr<DescriptorHeap>& GetNonShaderVisibleHeap() const { return m_nonShaderVisibleHeap; }

private:
    DescriptorHeapManager() = default;

    std::shared_ptr<DescriptorHeap> m_cbvSrvUavHeap;
    std::shared_ptr<DescriptorHeap> m_samplerHeap;
    std::shared_ptr<DescriptorHeap> m_rtvHeap;
    std::shared_ptr<DescriptorHeap> m_dsvHeap;
    std::shared_ptr<DescriptorHeap> m_nonShaderVisibleHeap;
};
