#include "Sampler.h"

std::shared_ptr<Sampler> Sampler::m_defaultSampler = nullptr;
std::shared_ptr<Sampler> Sampler::m_defaultShadowSampler = nullptr;

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
		m_defaultSampler = Sampler::CreateSampler(samplerDesc);
	}
	return m_defaultSampler;
}

std::shared_ptr<Sampler> Sampler::GetDefaultShadowSampler() {
	if (m_defaultShadowSampler == nullptr) {
		D3D12_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		samplerDesc.BorderColor[0] = 1.0f;  // Set the border color for shadow maps, 1.0 = full depth, no shadow
		samplerDesc.BorderColor[1] = 0.0f;
		samplerDesc.BorderColor[2] = 0.0f;
		samplerDesc.BorderColor[3] = 1.0f;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
		samplerDesc.MipLODBias = 0;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		m_defaultShadowSampler = Sampler::CreateSampler(samplerDesc);
	}
	return m_defaultShadowSampler;
}
