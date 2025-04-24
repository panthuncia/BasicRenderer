#pragma once

#include "Resources/Resource.h"
#include "Render/DescriptorHeap.h"
#include "spdlog/spdlog.h"
#include "Resources/HeapIndexInfo.h"

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

	void SetSRVDescriptors(std::shared_ptr<DescriptorHeap> pSRVHeap, const std::vector<ShaderVisibleIndexInfo>& srvInfos) {
		m_pSRVHeap = pSRVHeap;
		m_SRVInfos = srvInfos;
	}

	void SetUAVGPUDescriptors(std::shared_ptr<DescriptorHeap> pUAVHeap, const std::vector<ShaderVisibleIndexInfo>& uavInfos, unsigned int counterOffset = 0) {
		m_pUAVShaderVisibleHeap = pUAVHeap;
		m_UAVShaderVisibleInfos = uavInfos;
		m_counterOffset = counterOffset;
	}

	void SetUAVCPUDescriptors(std::shared_ptr<DescriptorHeap> pUAVHeap, const std::vector<NonShaderVisibleIndexInfo>& uavInfos) {
		m_pUAVNonShaderVisibleHeap = pUAVHeap;
		m_UAVNonShaderVisibleInfos = uavInfos;
	}

	void SetCBVDescriptor(std::shared_ptr<DescriptorHeap> pCBVHeap, const ShaderVisibleIndexInfo& cbvInfo) {
		m_pCBVHeap = pCBVHeap;
		m_CBVInfo = cbvInfo;
	}

	void SetRTVDescriptors(std::shared_ptr<DescriptorHeap> pRTVHeap, const std::vector<NonShaderVisibleIndexInfo>& rtvInfos) {
		m_pRTVHeap = pRTVHeap;
		m_RTVInfos = rtvInfos;
	}

	void SetDSVDescriptors(std::shared_ptr<DescriptorHeap> pDSVHeap, const std::vector<NonShaderVisibleIndexInfo>& dsvInfos) {
		m_pDSVHeap = pDSVHeap;
		m_DSVInfos = dsvInfos;
	}

	std::vector<ShaderVisibleIndexInfo>& GetSRVInfo() { return m_SRVInfos; }
	std::vector<ShaderVisibleIndexInfo>& GetUAVShaderVisibleInfo() { return m_UAVShaderVisibleInfos; }
	unsigned int GetUAVCounterOffset() { return m_counterOffset; }
	std::vector<NonShaderVisibleIndexInfo>& GetUAVNonShaderVisibleInfo() { return m_UAVNonShaderVisibleInfos; }
	ShaderVisibleIndexInfo& GetCBVInfo() { return m_CBVInfo; }
	std::vector<NonShaderVisibleIndexInfo>& GetRTVInfos() { return m_RTVInfos; }
	std::vector<NonShaderVisibleIndexInfo>& GetDSVInfos() { return m_DSVInfos; }
	~GloballyIndexedResource() {
		// Release SRV, UAV, and CBV
		if (m_pSRVHeap) {
			for (auto& srvInfo : m_SRVInfos) {
				m_pSRVHeap->ReleaseDescriptor(srvInfo.index);
			}
		}
		if (m_pUAVShaderVisibleHeap) {
			for (auto& uavInfo : m_UAVShaderVisibleInfos) {
				m_pUAVShaderVisibleHeap->ReleaseDescriptor(uavInfo.index);
			}
		}
		if (m_pUAVNonShaderVisibleHeap) {
			for (auto& uavInfo : m_UAVNonShaderVisibleInfos) {
				m_pUAVNonShaderVisibleHeap->ReleaseDescriptor(uavInfo.index);
			}
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
	std::vector<ShaderVisibleIndexInfo> m_SRVInfos;
	std::shared_ptr<DescriptorHeap> m_pSRVHeap = nullptr;
	std::vector<ShaderVisibleIndexInfo> m_UAVShaderVisibleInfos;
	std::vector<NonShaderVisibleIndexInfo> m_UAVNonShaderVisibleInfos;
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