#pragma once

#include <wrl.h>
#include <vector>
#include <variant>

#include <rhi.h>

#include "ShaderBuffers.h"
#include "spdlog/spdlog.h"
#include "Resources/Buffers/Buffer.h"
#include "Render/DescriptorHeap.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/DeviceManager.h"

using namespace Microsoft::WRL;

class BufferView;
class SortedUnsignedIntBuffer;

class ResourceManager {
public:

    struct ViewRequirements {
        struct TextureViews {
            // Resource shape
            uint32_t mipLevels = 1;
            bool isCubemap = false;
            bool isArray = false;
            uint32_t arraySize = 1;        // number of array elements (for cubemaps: number of cubes)
            uint32_t totalArraySlices = 1; // total slices (for cubemaps: arraySize * 6)

            // Formats
            rhi::Format baseFormat = rhi::Format::Unknown;
            rhi::Format srvFormat = rhi::Format::Unknown;
            rhi::Format uavFormat = rhi::Format::Unknown;
            rhi::Format rtvFormat = rhi::Format::Unknown;
            rhi::Format dsvFormat = rhi::Format::Unknown;

            // Which views to create
            bool createSRV = true;
            bool createUAV = false;
            bool createNonShaderVisibleUAV = false;
            bool createRTV = false;
            bool createDSV = false;

            // Extra (common for cubemaps): also create a Texture2DArray SRV view.
            bool createCubemapAsArraySRV = false;

            // UAV options
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

    void AssignDescriptorSlots(
        GloballyIndexedResource& target,
        rhi::Resource& apiResource,
        const ViewRequirements& req);

    static ResourceManager& GetInstance() {
        static ResourceManager instance;
        return instance;
    }

    void Initialize();
    void Cleanup();

    rhi::DescriptorHeap GetSRVDescriptorHeap();
    rhi::DescriptorHeap GetSamplerDescriptorHeap();
    void UpdatePerFrameBuffer(UINT cameraIndex, UINT numLights, DirectX::XMUINT2 screenRes, DirectX::XMUINT3 clusterSizes, unsigned int frameIndex);
    
    std::shared_ptr<Buffer>& GetPerFrameBuffer() {
		return m_perFrameBuffer;
    }

	void SetDirectionalCascadeSplits(const std::vector<float>& splits) {
        switch (perFrameCBData.numShadowCascades) {
        case 1:
            perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(splits[0], 0, 0, 0);
            break;
        case 2:
            perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(splits[0], splits[1], 0, 0);
            break;
        case 3:
            perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(splits[0], splits[1], splits[2], 0);
            break;
        case 4:
            perFrameCBData.shadowCascadeSplits = DirectX::XMVectorSet(splits[0], splits[1], splits[2], splits[3]);
        }
	}

    UINT CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc);

	void SetActiveEnvironmentIndex(unsigned int index) { perFrameCBData.activeEnvironmentIndex = index; }
	void SetOutputType(unsigned int type) { perFrameCBData.outputType = type; }

	rhi::Resource GetUAVCounterReset() { return m_uavCounterReset.Get(); }

    const std::shared_ptr<DescriptorHeap>& GetCBVSRVUAVHeap() const { return m_cbvSrvUavHeap; }
    const std::shared_ptr<DescriptorHeap>& GetSamplerHeap() const { return m_samplerHeap; }
    const std::shared_ptr<DescriptorHeap>& GetRTVHeap() const { return m_rtvHeap; }
    const std::shared_ptr<DescriptorHeap>& GetDSVHeap() const { return m_dsvHeap; }
    const std::shared_ptr<DescriptorHeap>& GetNonShaderVisibleHeap() const { return m_nonShaderVisibleHeap; }
    
private:
    ResourceManager(){};

    std::shared_ptr<DescriptorHeap> m_cbvSrvUavHeap;
    std::shared_ptr<DescriptorHeap> m_samplerHeap;
    std::shared_ptr<DescriptorHeap> m_rtvHeap;
    std::shared_ptr<DescriptorHeap> m_dsvHeap;
    std::shared_ptr<DescriptorHeap> m_nonShaderVisibleHeap;

    std::shared_ptr<Buffer> m_perFrameBuffer;
    UINT8* pPerFrameConstantBuffer;
    PerFrameCB perFrameCBData;
    UINT currentFrameIndex;

    rhi::ResourcePtr m_uavCounterReset;

	int defaultShadowSamplerIndex = -1;

};