#pragma once

#include "Resources/Resource.h"
#include "Render/DescriptorHeap.h"
#include "spdlog/spdlog.h"
#include "Resources/HeapIndexInfo.h"

class GloballyIndexedResourceBase : public Resource {
public:
	GloballyIndexedResourceBase() : Resource() {};
protected:
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

	void SetSRVDescriptors(std::shared_ptr<DescriptorHeap> pSRVHeap, const std::vector<std::vector<ShaderVisibleIndexInfo>>& srvInfos) {
		m_pSRVHeap = pSRVHeap;
		m_SRVInfos = srvInfos;
	}

	void SetUAVGPUDescriptors(std::shared_ptr<DescriptorHeap> pUAVHeap, const std::vector<std::vector<ShaderVisibleIndexInfo>>& uavInfos, unsigned int counterOffset = 0) {
		m_pUAVShaderVisibleHeap = pUAVHeap;
		m_UAVShaderVisibleInfos = uavInfos;
		m_counterOffset = counterOffset;
	}

	void SetUAVCPUDescriptors(std::shared_ptr<DescriptorHeap> pUAVHeap, const std::vector<std::vector<NonShaderVisibleIndexInfo>>& uavInfos) {
		m_pUAVNonShaderVisibleHeap = pUAVHeap;
		m_UAVNonShaderVisibleInfos = uavInfos;
	}

	void SetCBVDescriptor(std::shared_ptr<DescriptorHeap> pCBVHeap, const ShaderVisibleIndexInfo& cbvInfo) {
		m_pCBVHeap = pCBVHeap;
		m_CBVInfo = cbvInfo;
	}

	void SetRTVDescriptors(std::shared_ptr<DescriptorHeap> pRTVHeap, const std::vector<std::vector<NonShaderVisibleIndexInfo>>& rtvInfos) {
		m_pRTVHeap = pRTVHeap;
		m_RTVInfos = rtvInfos;
	}

	void SetDSVDescriptors(std::shared_ptr<DescriptorHeap> pDSVHeap, const std::vector<std::vector<NonShaderVisibleIndexInfo>>& dsvInfos) {
		m_pDSVHeap = pDSVHeap;
		m_DSVInfos = dsvInfos;
	}

	const ShaderVisibleIndexInfo& GetSRVInfo(unsigned int mip, unsigned int slice = 0) { return m_SRVInfos[slice][mip]; }
	unsigned int GetNumSRVMipLevels() { return static_cast<unsigned int>(m_SRVInfos[0].size()); }
	unsigned int GetNumSRSlices() { return static_cast<unsigned int>(m_SRVInfos.size()); }
	const ShaderVisibleIndexInfo& GetUAVShaderVisibleInfo(unsigned int mip, unsigned int slice = 0) { return m_UAVShaderVisibleInfos[slice][mip]; }
	unsigned int GetUAVCounterOffset() { return m_counterOffset; }
	unsigned int GetNumUAVMipLevels() { return static_cast<unsigned int>(m_UAVShaderVisibleInfos[0].size()); }
	unsigned int GetNumUAVSlices() { return static_cast<unsigned int>(m_UAVShaderVisibleInfos.size()); }
	const NonShaderVisibleIndexInfo& GetUAVNonShaderVisibleInfo(unsigned int mip, unsigned int slice = 0) { return m_UAVNonShaderVisibleInfos[slice][mip]; }
	ShaderVisibleIndexInfo& GetCBVInfo() { return m_CBVInfo; }
	NonShaderVisibleIndexInfo& GetRTVInfo(unsigned int mip, unsigned int slice = 0) { return m_RTVInfos[slice][mip]; }
	unsigned int GetNumRTVMipLevels() { return static_cast<unsigned int>(m_RTVInfos[0].size()); }
	unsigned int GetNumRTVSlices() { return static_cast<unsigned int>(m_RTVInfos.size()); }
	NonShaderVisibleIndexInfo& GetDSVInfo(unsigned int mip, unsigned int slice = 0) { return m_DSVInfos[slice][mip]; }
	unsigned int GetNumDSVMipLevels() { return static_cast<unsigned int>(m_DSVInfos[0].size()); }
	unsigned int GetNumDSVSlices() { return static_cast<unsigned int>(m_DSVInfos.size()); }
	~GloballyIndexedResource() {
		// Release SRV, UAV, and CBV
		if (m_pSRVHeap) {
			for (auto& srvInfo : m_SRVInfos) {
				for (auto& srvInfo : srvInfo) {
					m_pSRVHeap->ReleaseDescriptor(srvInfo.index);
				}
			}
		}
		if (m_pUAVShaderVisibleHeap) {
			for (auto& uavInfo : m_UAVShaderVisibleInfos) {
				for (auto& uavInfo : uavInfo) {
					m_pUAVShaderVisibleHeap->ReleaseDescriptor(uavInfo.index);
				}
			}
		}
		if (m_pUAVNonShaderVisibleHeap) {
			for (auto& uavInfo : m_UAVNonShaderVisibleInfos) {
				for (auto& uavInfo : uavInfo) {
					// Release the non-shader visible UAVs
					m_pUAVNonShaderVisibleHeap->ReleaseDescriptor(uavInfo.index);
				}
			}
		}
		if (m_pCBVHeap) {
			m_pCBVHeap->ReleaseDescriptor(m_CBVInfo.index);
		}

		// Release RTVs and DSVs
		if (m_pRTVHeap) {
			for (auto& rtvInfo : m_RTVInfos) {
				for (auto& rtvInfo : rtvInfo) {
					m_pRTVHeap->ReleaseDescriptor(rtvInfo.index);
				}
			}
		}

		if (m_pDSVHeap) {
			for (auto& dsvInfo : m_DSVInfos) {
				for (auto& dsvInfo : dsvInfo) {
					m_pDSVHeap->ReleaseDescriptor(dsvInfo.index);
				}
			}
		}
	};
protected:
	virtual void OnSetName() override {}
private:
	std::vector<std::vector<ShaderVisibleIndexInfo>> m_SRVInfos;
	std::shared_ptr<DescriptorHeap> m_pSRVHeap = nullptr;
	std::vector<std::vector<ShaderVisibleIndexInfo>> m_UAVShaderVisibleInfos;
	std::vector<std::vector<NonShaderVisibleIndexInfo>> m_UAVNonShaderVisibleInfos;
	std::shared_ptr<DescriptorHeap> m_pUAVShaderVisibleHeap = nullptr;
	std::shared_ptr<DescriptorHeap> m_pUAVNonShaderVisibleHeap = nullptr;
	ShaderVisibleIndexInfo m_CBVInfo;
	std::shared_ptr<DescriptorHeap> m_pCBVHeap = nullptr;
	std::vector<std::vector<NonShaderVisibleIndexInfo>> m_RTVInfos;
	std::shared_ptr<DescriptorHeap> m_pRTVHeap = nullptr;
	std::vector<std::vector<NonShaderVisibleIndexInfo>> m_DSVInfos;
	std::shared_ptr<DescriptorHeap> m_pDSVHeap = nullptr;
	unsigned int m_counterOffset = 0;

	friend class DynamicGloballyIndexedResource;
};