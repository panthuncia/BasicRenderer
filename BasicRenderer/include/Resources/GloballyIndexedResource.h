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

enum class SRVViewType : int {
	Invalid = -1,
	Buffer,
	Texture2D,
	Texture2DArray,
	TextureCube,
	TextureCubeArray,
	NumSRVViewTypes
};

class GloballyIndexedResource : public GloballyIndexedResourceBase
{
public:
	GloballyIndexedResource(std::wstring name = L"") :
		GloballyIndexedResourceBase() {
		if (name != L"") {
			SetName(name);
		}
		m_SRVViews.resize(static_cast<unsigned int>(SRVViewType::NumSRVViewTypes));
	};

	void SetSRVView(
		SRVViewType type,
		std::shared_ptr<DescriptorHeap> heap,
		std::vector<std::vector<ShaderVisibleIndexInfo>> const & infos
	) {
		if (type == SRVViewType::Buffer) {
			m_primaryViewType = SRVViewType::Buffer;
		}
		m_SRVViews[static_cast<unsigned int>(type)] = {heap, infos};
	}

	void SetUAVGPUDescriptors(std::shared_ptr<DescriptorHeap> pUAVHeap, const std::vector<std::vector<ShaderVisibleIndexInfo>>& uavInfos, size_t counterOffset = 0) {
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


	const ShaderVisibleIndexInfo& GetSRVInfo(unsigned int mip, unsigned int slice = 0) { 
		auto& info = GetDefaultSRVInfo();
		return info[slice][mip]; 
	}

	const ShaderVisibleIndexInfo& GetSRVInfo(SRVViewType type, unsigned int mip, unsigned int slice = 0) {
		return m_SRVViews[static_cast<unsigned int>(type)].infos[slice][mip];
	}

	unsigned int GetNumSRVMipLevels() {
		auto& info = GetDefaultSRVInfo();
		return static_cast<unsigned int>(info[0].size());
	}

	unsigned int GetNumSRVSlices() { 
		auto& info = GetDefaultSRVInfo();
		return static_cast<unsigned int>(info.size()); 
	}

	unsigned int GetNumSRVSlices(SRVViewType type) {
		return static_cast<unsigned int>(m_SRVViews[static_cast<unsigned int>(type)].infos.size());
	}

	unsigned int GetNumSRVMipLevels(SRVViewType type) {
		return static_cast<unsigned int>(m_SRVViews[static_cast<unsigned int>(type)].infos[0].size());
	}

	const ShaderVisibleIndexInfo& GetUAVShaderVisibleInfo(unsigned int mip, unsigned int slice = 0) { return m_UAVShaderVisibleInfos[slice][mip]; }
	size_t GetUAVCounterOffset() { return m_counterOffset; }
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

	void SetDefaultSRVViewType(SRVViewType type) {
		if (type >= SRVViewType::NumSRVViewTypes) {
			spdlog::error("Invalid SRV view type specified.");
			return;
		}
		m_primaryViewType = type;
	}

	virtual ~GloballyIndexedResource() {
		// Release SRV, UAV, and CBV
		if (m_pSRVHeap) {
			for (unsigned int i = 0; i < m_SRVViews.size(); i++) {
				if (m_SRVViews[i].heap == nullptr) {
					continue;
				}
				for (auto& srvInfos : m_SRVViews[i].infos) {
					for (auto& srvInfo : srvInfos) {
						m_pSRVHeap->ReleaseDescriptor(srvInfo.index);
					}
				}
			}
		}
		if (m_pUAVShaderVisibleHeap) {
			for (auto& uavInfos : m_UAVShaderVisibleInfos) {
				for (auto& uavInfo : uavInfos) {
					m_pUAVShaderVisibleHeap->ReleaseDescriptor(uavInfo.index);
				}
			}
		}
		if (m_pUAVNonShaderVisibleHeap) {
			for (auto& uavInfos : m_UAVNonShaderVisibleInfos) {
				for (auto& uavInfo : uavInfos) {
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
			for (auto& rtvInfos : m_RTVInfos) {
				for (auto& rtvInfo : rtvInfos) {
					m_pRTVHeap->ReleaseDescriptor(rtvInfo.index);
				}
			}
		}

		if (m_pDSVHeap) {
			for (auto& dsvInfos : m_DSVInfos) {
				for (auto& dsvInfo : dsvInfos) {
					m_pDSVHeap->ReleaseDescriptor(dsvInfo.index);
				}
			}
		}
	};
protected:
	virtual void OnSetName() override {}
private:
	struct SRVView {
		std::shared_ptr<DescriptorHeap> heap = nullptr;
		std::vector<std::vector<ShaderVisibleIndexInfo>> infos;
	};
	std::vector<SRVView> m_SRVViews;
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
	size_t m_counterOffset = 0;

	SRVViewType m_primaryViewType = SRVViewType::Invalid;

	const std::vector<std::vector<ShaderVisibleIndexInfo>>& GetDefaultSRVInfo() {
		if (m_primaryViewType == SRVViewType::Invalid) {
			spdlog::error("Primary SRV view type is not set. Please set it before accessing a default SRV info.");
			throw std::runtime_error("Primary SRV view type is not set.");
		}
		return m_SRVViews[static_cast<unsigned int>(m_primaryViewType)].infos;
	}

	friend class DynamicGloballyIndexedResource;
};