#pragma once

#include <unordered_map>
#include <wrl/client.h>
#include <d3d12.h>
#include <string>

class CommandSignatureManager {
public:
	static CommandSignatureManager& GetInstance();
	void Initialize();
	ID3D12CommandSignature* GetDispatchMeshCommandSignature();
	ID3D12CommandSignature* GetDispatchCommandSignature() { return m_dispatchCommandSignature.Get(); }

private:
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_dispatchMeshCommandSignature;
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_dispatchCommandSignature;
};

inline CommandSignatureManager& CommandSignatureManager::GetInstance() {
	static CommandSignatureManager instance;
	return instance;
}