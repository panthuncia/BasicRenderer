#pragma once

#include "Resources/Resource.h"
#include "DescriptorHeap.h"
#include "spdlog/spdlog.h"
#include "HeapIndexInfo.h"

class GloballyIndexedResourceBase : public Resource {
public:
	GloballyIndexedResourceBase() : Resource() {};
protected:
	virtual std::vector<D3D12_RESOURCE_BARRIER>& GetTransitions(ResourceState prevState, ResourceState newState) override = 0;
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

	void SetSRVDescriptor(std::shared_ptr<DescriptorHeap> pSRVHeap, ShaderVisibleIndexInfo srvInfo) {
		m_pSRVHeap = pSRVHeap;
		m_SRVInfo = srvInfo;
	}

	void SetUAVGPUDescriptor(std::shared_ptr<DescriptorHeap> pUAVHeap, ShaderVisibleIndexInfo uavInfo, unsigned int counterOffset = 0) {
		m_pUAVShaderVisibleHeap = pUAVHeap;
		m_UAVShaderVisibleInfo = uavInfo;
		m_counterOffset = counterOffset;
	}

	void SetUAVCPUDescriptor(std::shared_ptr<DescriptorHeap> pUAVHeap, NonShaderVisibleIndexInfo uavInfo) {
		m_pUAVNonShaderVisibleHeap = pUAVHeap;
		m_UAVNonShaderVisibleInfo = uavInfo;
	}

	void SetCBVDescriptor(std::shared_ptr<DescriptorHeap> pCBVHeap, ShaderVisibleIndexInfo cbvInfo) {
		m_pCBVHeap = pCBVHeap;
		m_CBVInfo = cbvInfo;
	}

	void SetRTVDescriptors(std::shared_ptr<DescriptorHeap> pRTVHeap, std::vector<NonShaderVisibleIndexInfo>& rtvInfos) {
		m_pRTVHeap = pRTVHeap;
		m_RTVInfos = rtvInfos;
	}

	void SetDSVDescriptors(std::shared_ptr<DescriptorHeap> pDSVHeap, std::vector<NonShaderVisibleIndexInfo>& dsvInfos) {
		m_pDSVHeap = pDSVHeap;
		m_DSVInfos = dsvInfos;
	}

	ShaderVisibleIndexInfo& GetSRVInfo() { return m_SRVInfo; }
	ShaderVisibleIndexInfo& GetUAVShaderVisibleInfo() { return m_UAVShaderVisibleInfo; }
	unsigned int GetUAVCounterOffset() { return m_counterOffset; }
	NonShaderVisibleIndexInfo& GetUAVNonShaderVisibleInfo() { return m_UAVNonShaderVisibleInfo; }
	ShaderVisibleIndexInfo& GetCBVInfo() { return m_CBVInfo; }
	std::vector<NonShaderVisibleIndexInfo>& GetRTVInfos() { return m_RTVInfos; }
	std::vector<NonShaderVisibleIndexInfo>& GetDSVInfos() { return m_DSVInfos; }
	~GloballyIndexedResource() {
		// Release SRV, UAV, and CBV
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
	};
protected:
	virtual void OnSetName() override {}
private:
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
	unsigned int m_counterOffset = 0;

	friend class DynamicGloballyIndexedResource;
};