#pragma once

#include <unordered_map>
#include <wrl/client.h>
#include <d3d12.h>
#include <string>

#include <rhi.h>

class CommandSignatureManager {
public:
	static CommandSignatureManager& GetInstance();
	void Initialize();
	const rhi::CommandSignature& GetDispatchMeshCommandSignature();
	const rhi::CommandSignature& GetDispatchCommandSignature();

private:
	rhi::CommandSignaturePtr m_dispatchMeshCommandSignature;
	rhi::CommandSignaturePtr m_dispatchCommandSignature;
};

inline CommandSignatureManager& CommandSignatureManager::GetInstance() {
	static CommandSignatureManager instance;
	return instance;
}