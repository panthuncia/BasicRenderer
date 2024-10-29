#pragma once

#include "Resource.h"
#include "DescriptorHeap.h"
#include "spdlog/spdlog.h"

class GloballyIndexedResourceBase : public Resource {
public:
	GloballyIndexedResourceBase() : Resource() {};
protected:
	virtual void Transition(ID3D12GraphicsCommandList* commandList, ResourceState prevState, ResourceState newState) override = 0;
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

	void SetSRVDescriptor(DescriptorHeap* pSRVHeap, ShaderVisibleIndexInfo srvInfo) {
		m_pSRVHeap = pSRVHeap;
		m_SRVInfo = srvInfo;
	}

	void SetUAVDescriptor(DescriptorHeap* pUAVHeap, ShaderVisibleIndexInfo uavInfo) {
		m_pUAVHeap = pUAVHeap;
		m_UAVInfo = uavInfo;
	}

	void SetCBVDescriptor(DescriptorHeap* pCBVHeap, ShaderVisibleIndexInfo cbvInfo) {
		m_pCBVHeap = pCBVHeap;
		m_CBVInfo = cbvInfo;
	}

	void SetRTVDescriptors(DescriptorHeap* pRTVHeap, std::vector<NonShaderVisibleIndexInfo>& rtvInfos) {
		m_pRTVHeap = pRTVHeap;
		m_RTVInfos = rtvInfos;
	}

	void SetDSVDescriptors(DescriptorHeap* pDSVHeap, std::vector<NonShaderVisibleIndexInfo>& dsvInfos) {
		m_pDSVHeap = pDSVHeap;
		m_DSVInfos = dsvInfos;
	}

	ShaderVisibleIndexInfo& GetSRVInfo() { return m_SRVInfo; }
	ShaderVisibleIndexInfo& GetUAVInfo() { return m_UAVInfo; }
	ShaderVisibleIndexInfo& GetCBVInfo() { return m_CBVInfo; }
	std::vector<NonShaderVisibleIndexInfo>& GetRTVInfos() { return m_RTVInfos; }
	std::vector<NonShaderVisibleIndexInfo>& GetDSVInfos() { return m_DSVInfos; }
	~GloballyIndexedResource() {
		// Release SRV, UAV, and CBV
		if (m_pSRVHeap) {
			m_pSRVHeap->ReleaseDescriptor(m_SRVInfo.index);
		}
		else {
			spdlog::info("GloballyIndexedResource::Destructor: No SRV heap set.");
		}
		if (m_pUAVHeap) {
			m_pUAVHeap->ReleaseDescriptor(m_UAVInfo.index);
		}
		else {
			spdlog::info("GloballyIndexedResource::Destructor: No UAV heap set.");
		}
		if (m_pCBVHeap) {
			m_pCBVHeap->ReleaseDescriptor(m_CBVInfo.index);
		}
		else {
			spdlog::info("GloballyIndexedResource::Destructor: No CBV heap set.");
		}

		// Release RTVs and DSVs
		if (m_pRTVHeap) {
			for (auto& rtvInfo : m_RTVInfos) {
				m_pRTVHeap->ReleaseDescriptor(rtvInfo.index);
			}
		}
		else {
			spdlog::info("GloballyIndexedResource::Destructor: No RTV heap set.");
		}

		if (m_pDSVHeap) {
			for (auto& dsvInfo : m_DSVInfos) {
				m_pDSVHeap->ReleaseDescriptor(dsvInfo.index);
			}
		}
		else {
			spdlog::info("GloballyIndexedResource::Destructor: No DSV heap set.");
		}
	};
protected:
	virtual void OnSetName() override {}
private:
	ShaderVisibleIndexInfo m_SRVInfo;
	DescriptorHeap* m_pSRVHeap = nullptr;
	ShaderVisibleIndexInfo m_UAVInfo;
	DescriptorHeap* m_pUAVHeap = nullptr;
	ShaderVisibleIndexInfo m_CBVInfo;
	DescriptorHeap* m_pCBVHeap = nullptr;
	std::vector<NonShaderVisibleIndexInfo> m_RTVInfos;
	DescriptorHeap* m_pRTVHeap = nullptr;
	std::vector<NonShaderVisibleIndexInfo> m_DSVInfos;
	DescriptorHeap* m_pDSVHeap = nullptr;

	friend class DynamicGloballyIndexedResource;
};