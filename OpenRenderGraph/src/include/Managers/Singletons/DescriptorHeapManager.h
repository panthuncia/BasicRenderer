#pragma once

#include <memory>
#include <variant>
#include <vector>

#include <rhi.h>

#include "Render/DescriptorHeap.h"
#include "Render/Runtime/DescriptorServiceTypes.h"

class GloballyIndexedResource;

class DescriptorHeapManager {
public:
	using ViewRequirements = rg::runtime::DescriptorViewRequirements;

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
