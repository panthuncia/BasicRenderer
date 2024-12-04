#pragma once

#include "Resource.h"
#include "DescriptorHeap.h"
#include "spdlog/spdlog.h"
#include "HeapIndexInfo.h"
#include "DeletionManager.h"

class GloballyIndexedResourceBase : public Resource {
public:
	GloballyIndexedResourceBase() : Resource() {};
protected:
	virtual std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(uint8_t frameIndex, ResourceState prevState, ResourceState newState) override = 0;
};

class ResourceIndexInfo {
public:
	ResourceIndexInfo() {}
	~ResourceIndexInfo() {
		if (m_pSRVHeap) {
			m_pSRVHeap->ReleaseDescriptor(m_SRVInfo.index);
		}
		if (m_pUAVShaderVisibleHeap) {
			m_pUAVShaderVisibleHeap->ReleaseDescriptor(m_UAVShaderVisibleInfo.index);
		}
		if (m_pUAVNonShaderVisibleHeap) {
			m_pUAVNonShaderVisibleHeap->ReleaseDescriptor(m_UAVNonShaderVisibleInfo.index);
		}
		if (m_pCBVHeap) {
			m_pCBVHeap->ReleaseDescriptor(m_CBVInfo.index);
		}

		// Release RTVs and DSVs
		if (m_pRTVHeap) {
			for (auto& rtvInfo : m_RTVInfos) {
				m_pRTVHeap->ReleaseDescriptor(rtvInfo.index);
			}
		}

		if (m_pDSVHeap) {
			for (auto& dsvInfo : m_DSVInfos) {
				m_pDSVHeap->ReleaseDescriptor(dsvInfo.index);
			}
		}
	}
	ShaderVisibleIndexInfo m_SRVInfo;
	std::shared_ptr<DescriptorHeap> m_pSRVHeap = nullptr;
	ShaderVisibleIndexInfo m_UAVShaderVisibleInfo;
	NonShaderVisibleIndexInfo m_UAVNonShaderVisibleInfo;
	std::shared_ptr<DescriptorHeap> m_pUAVShaderVisibleHeap = nullptr;
	std::shared_ptr<DescriptorHeap> m_pUAVNonShaderVisibleHeap = nullptr;
	ShaderVisibleIndexInfo m_CBVInfo;
	std::shared_ptr<DescriptorHeap> m_pCBVHeap = nullptr;
	std::vector<NonShaderVisibleIndexInfo> m_RTVInfos;
	std::shared_ptr<DescriptorHeap> m_pRTVHeap = nullptr;
	std::vector<NonShaderVisibleIndexInfo> m_DSVInfos;
	std::shared_ptr<DescriptorHeap> m_pDSVHeap = nullptr;
};

class GloballyIndexedResource : public GloballyIndexedResourceBase
{
public:
	GloballyIndexedResource(std::wstring name = L"") :
		GloballyIndexedResourceBase() {
		if (name != L"") {
			SetName(name);
		}
	};

	void SetIndexInfo(std::vector<std::shared_ptr<ResourceIndexInfo>>& infos, size_t uavCounterOffset = 0) {
		// Queue old descriptors for deletion
		auto& deletionManager = DeletionManager::GetInstance();
		for (auto& info : m_indexInfos) {
			deletionManager.MarkForDelete(info);
		}
		m_indexInfos = infos;
		m_counterOffset = uavCounterOffset;
	}

	ShaderVisibleIndexInfo& GetSRVInfo(uint8_t frameIndex) { return m_indexInfos[frameIndex]->m_SRVInfo; }
	ShaderVisibleIndexInfo& GetUAVShaderVisibleInfo(uint8_t frameIndex) { return m_indexInfos[frameIndex]->m_UAVShaderVisibleInfo; }
	unsigned int GetUAVCounterOffset() { return m_counterOffset; }
	NonShaderVisibleIndexInfo& GetUAVNonShaderVisibleInfo(uint8_t frameIndex) { return m_indexInfos[frameIndex]->m_UAVNonShaderVisibleInfo; }
	ShaderVisibleIndexInfo& GetCBVInfo(uint8_t frameIndex) { return m_indexInfos[frameIndex]->m_CBVInfo; }
	std::vector<NonShaderVisibleIndexInfo>& GetRTVInfos(uint8_t frameIndex) { return m_indexInfos[frameIndex]->m_RTVInfos; }
	std::vector<NonShaderVisibleIndexInfo>& GetDSVInfos(uint8_t frameIndex) { return m_indexInfos[frameIndex]->m_DSVInfos; }
	~GloballyIndexedResource() {
		// Queue for release
		auto& deletionManager = DeletionManager::GetInstance();
		for (auto& info : m_indexInfos) {
			deletionManager.MarkForDelete(info);
		}
	};
protected:
	virtual void OnSetName() override {}
private:

	std::vector<std::shared_ptr<ResourceIndexInfo>> m_indexInfos;
	unsigned int m_counterOffset = 0;

	friend class DynamicGloballyIndexedResource;
};