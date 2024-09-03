#include "Sampler.h"

std::shared_ptr<Sampler> Sampler::m_defaultSampler = nullptr;

std::shared_ptr<Sampler> Sampler::GetDefaultSampler() {
	if (m_defaultSampler == nullptr) {
		D3D12_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		samplerDesc.MipLODBias = 0;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		m_defaultSampler = std::make_shared<Sampler>(samplerDesc);
	}
	return m_defaultSampler;
}