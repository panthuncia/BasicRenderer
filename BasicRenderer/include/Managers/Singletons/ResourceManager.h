#pragma once

#include <wrl.h>
#include <vector>
#include <stdexcept>
#include <variant>

#include <rhi.h>
#include <rhi_helpers.h>

#include "ShaderBuffers.h"
#include "spdlog/spdlog.h"
#include "Resources/PixelBuffer.h"
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

            // Some resources (e.g. structured buffers) track the counter offset separately.
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

    template<typename T>
    std::shared_ptr<Buffer> CreateIndexedConstantBuffer(std::wstring name = L"") {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for constant buffers.");

        auto device = DeviceManager::GetInstance().GetDevice();

        // Calculate the size of the buffer to be 256-byte aligned
        UINT bufferSize = (sizeof(T) + 255) & ~255;

        // Create the buffer
        auto dataBuffer = Buffer::CreateShared(rhi::HeapType::DeviceLocal, bufferSize, false);
		dataBuffer->SetName(name);

		rhi::CbvDesc cbvDesc = {};
		cbvDesc.byteOffset = 0;
		cbvDesc.byteSize = bufferSize;

        UINT index = m_cbvSrvUavHeap->AllocateDescriptor();

        ShaderVisibleIndexInfo cbvInfo;
        cbvInfo.slot.index = index;
		cbvInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();
        dataBuffer->SetCBVDescriptor(m_cbvSrvUavHeap, cbvInfo);

		device.CreateConstantBufferView({ m_cbvSrvUavHeap->GetHeap().GetHandle(), index}, dataBuffer->GetAPIResource().GetHandle(), cbvDesc);

        return dataBuffer;
    }

    std::shared_ptr<Buffer> CreateIndexedStructuredBuffer(size_t numElements, unsigned int elementSize, bool UAV = false, bool UAVCounter = false) {
        auto device = DeviceManager::GetInstance().GetDevice();
        size_t bufferSize = numElements * elementSize;
        size_t counterOffset = 0;
		if (UAVCounter) {
            size_t requiredSize = (numElements * elementSize) + sizeof(UINT); // Add space for the counter
            unsigned int alignment = elementSize; // Buffer should be a multiple of sizeof(T)

            // Ensure bufferSize is a multiple of typeSize and meets requiredSize
            bufferSize = ((requiredSize + alignment - 1) / alignment) * alignment;

            // Find the next 4096-aligned address after requiredSize
            size_t potentialCounterOffset = (requiredSize + 4095) & ~4095;

            // If the 4096-aligned address is within the buffer, we can use it
            if (potentialCounterOffset + sizeof(unsigned int) <= bufferSize) {
                counterOffset = potentialCounterOffset;
            }
            else {
                // Otherwise, expand the buffer to fit the 4096-aligned counter offset
                bufferSize = ((potentialCounterOffset + sizeof(unsigned int) + alignment - 1) / alignment) * alignment;
                counterOffset = potentialCounterOffset;
            }

            assert(counterOffset % 4096 == 0);
		}

        auto dataBuffer = Buffer::CreateShared(rhi::HeapType::DeviceLocal, bufferSize, UAV);

        unsigned int index = m_cbvSrvUavHeap->AllocateDescriptor();

		rhi::SrvDesc srvDesc = {};
		srvDesc.formatOverride = rhi::Format::Unknown;
		srvDesc.dimension = rhi::SrvDim::Buffer;
		srvDesc.buffer.numElements = static_cast<UINT>(numElements);
		srvDesc.buffer.structureByteStride = static_cast<UINT>(elementSize);
		srvDesc.buffer.kind = rhi::BufferViewKind::Structured;

        device.CreateShaderResourceView({ m_cbvSrvUavHeap->GetHeap().GetHandle(), index}, dataBuffer->GetAPIResource().GetHandle(), srvDesc);

        ShaderVisibleIndexInfo srvInfo;
        srvInfo.slot.index = index;
		srvInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();
        dataBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, {{srvInfo}});

        if (UAV) {
            rhi::UavDesc uavDesc = {};
            uavDesc.formatOverride = rhi::Format::Unknown;
            uavDesc.dimension = rhi::UavDim::Buffer;
			uavDesc.buffer.kind = rhi::BufferViewKind::Structured;
			uavDesc.buffer.numElements = static_cast<UINT>(numElements);
			uavDesc.buffer.structureByteStride = static_cast<UINT>(elementSize);
            if (UAVCounter) {
                uavDesc.buffer.counterOffsetInBytes = counterOffset;
            }
            else {
                uavDesc.buffer.counterOffsetInBytes = 0;
			}

			// Shader visible UAV
			unsigned int uavShaderVisibleIndex = m_cbvSrvUavHeap->AllocateDescriptor();
			device.CreateUnorderedAccessView({ m_cbvSrvUavHeap->GetHeap().GetHandle(), uavShaderVisibleIndex}, dataBuffer->GetAPIResource().GetHandle(), uavDesc);

			ShaderVisibleIndexInfo uavInfo;
			uavInfo.slot.index = uavShaderVisibleIndex;
			uavInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();
            dataBuffer->SetUAVGPUDescriptors(m_cbvSrvUavHeap, {{uavInfo}}, counterOffset);

			// Non-shader visible UAV
			unsigned int uavNonShaderVisibleIndex = m_nonShaderVisibleHeap->AllocateDescriptor();
			device.CreateUnorderedAccessView({ m_nonShaderVisibleHeap->GetHeap().GetHandle(), uavNonShaderVisibleIndex }, dataBuffer->GetAPIResource().GetHandle(), uavDesc);

			NonShaderVisibleIndexInfo uavNonShaderInfo;
			uavNonShaderInfo.slot.index = uavNonShaderVisibleIndex;
            uavNonShaderInfo.slot.heap = m_nonShaderVisibleHeap->GetHeap().GetHandle();
			dataBuffer->SetUAVCPUDescriptors(m_nonShaderVisibleHeap, { {uavNonShaderInfo} });

        }

        return dataBuffer;
    }

    std::shared_ptr<Buffer> CreateIndexedTypedBuffer(
        size_t        numElements,
        rhi::Format   elementFormat,
        bool          UAV = false)
    {
        auto device = DeviceManager::GetInstance().GetDevice();

        const size_t elementSize = rhi::helpers::BytesPerBlock(elementFormat);
        assert(elementFormat != rhi::Format::Unknown && "Typed buffers require a concrete format");
        assert(elementSize > 0 && "Unsupported/invalid format for typed buffer");

        const size_t bufferSize = numElements * elementSize;

        auto dataBuffer = Buffer::CreateShared(rhi::HeapType::DeviceLocal, bufferSize, UAV);

        unsigned int srvIndex = m_cbvSrvUavHeap->AllocateDescriptor();

        rhi::SrvDesc srvDesc = {};
        srvDesc.formatOverride = elementFormat; // required for typed views
        srvDesc.dimension = rhi::SrvDim::Buffer;
        srvDesc.buffer.kind = rhi::BufferViewKind::Typed;
        srvDesc.buffer.numElements = static_cast<UINT>(numElements);
        srvDesc.buffer.structureByteStride = 0; // ignored for typed

        device.CreateShaderResourceView({ m_cbvSrvUavHeap->GetHeap().GetHandle(), srvIndex }, dataBuffer->GetAPIResource().GetHandle(), srvDesc);

        ShaderVisibleIndexInfo srvInfo{};
        srvInfo.slot.index = srvIndex;
        srvInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();
        dataBuffer->SetSRVView(SRVViewType::Buffer, m_cbvSrvUavHeap, { { srvInfo } });

        if (UAV)
        {
            rhi::UavDesc uavDesc = {};
            uavDesc.formatOverride = elementFormat;      // required for typed UAV
            uavDesc.dimension = rhi::UavDim::Buffer;
            uavDesc.buffer.kind = rhi::BufferViewKind::Typed;
            uavDesc.buffer.numElements = static_cast<UINT>(numElements);
            uavDesc.buffer.structureByteStride = 0;                // ignored for typed
            uavDesc.buffer.counterOffsetInBytes = 0;               // no counters for typed UAVs

            // Shader-visible UAV
            unsigned int uavShaderVisibleIndex = m_cbvSrvUavHeap->AllocateDescriptor();
            device.CreateUnorderedAccessView({ m_cbvSrvUavHeap->GetHeap().GetHandle(), uavShaderVisibleIndex }, dataBuffer->GetAPIResource().GetHandle(), uavDesc);

            ShaderVisibleIndexInfo uavInfo{};
            uavInfo.slot.index = uavShaderVisibleIndex;
            uavInfo.slot.heap = m_cbvSrvUavHeap->GetHeap().GetHandle();
            // Pass 0 as "counter offset" because typed UAVs cannot have counters
            dataBuffer->SetUAVGPUDescriptors(m_cbvSrvUavHeap, { { uavInfo } }, /*counterOffset*/ 0);

            // Non-shader-visible UAV (for clears / CPU-side ops)
            unsigned int uavNonShaderVisibleIndex = m_nonShaderVisibleHeap->AllocateDescriptor();
            device.CreateUnorderedAccessView({ m_nonShaderVisibleHeap->GetHeap().GetHandle(), uavNonShaderVisibleIndex }, dataBuffer->GetAPIResource().GetHandle(), uavDesc);

            NonShaderVisibleIndexInfo uavNonShaderInfo{};
            uavNonShaderInfo.slot.index = uavNonShaderVisibleIndex;
            uavNonShaderInfo.slot.heap = m_nonShaderVisibleHeap->GetHeap().GetHandle();
            dataBuffer->SetUAVCPUDescriptors(m_nonShaderVisibleHeap, { { uavNonShaderInfo } });
        }

        return dataBuffer;
    }

    template<typename T>
    std::shared_ptr<Buffer> CreateConstantBuffer(std::wstring name = L"") {
        static_assert(std::is_standard_layout<T>::value, "T must be a standard layout type for constant buffers.");

		auto device = DeviceManager::GetInstance().GetDevice();

		// Calculate the size of the buffer to be 256-byte aligned
		UINT bufferSize = (sizeof(T) + 255) & ~255;

        auto dataBuffer = Buffer::CreateShared(device, rhi::HeapType::DeviceLocal, bufferSize, false);
		dataBuffer->SetName(name);


        return dataBuffer;
    }

    UINT CreateIndexedSampler(const rhi::SamplerDesc& samplerDesc);

	//void QueueResourceTransition(const ResourceTransition& transition);
    //void ExecuteResourceTransitions();

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