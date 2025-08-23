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
	rhi::CommandSignatureHandle& GetDispatchMeshCommandSignature();
	rhi::CommandSignatureHandle& GetDispatchCommandSignature() { return m_dispatchCommandSignature; }

private:
	rhi::CommandSignatureHandle m_dispatchMeshCommandSignature;
	rhi::CommandSignatureHandle m_dispatchCommandSignature;
};

inline CommandSignatureManager& CommandSignatureManager::GetInstance() {
	static CommandSignatureManager instance;
	return instance;
}