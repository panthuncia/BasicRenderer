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

private:
	Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_dispatchMeshCommandSignature;
};

inline CommandSignatureManager& CommandSignatureManager::GetInstance() {
	static CommandSignatureManager instance;
	return instance;
}