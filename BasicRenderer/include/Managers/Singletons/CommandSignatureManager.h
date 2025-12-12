#pragma once

#include <unordered_map>
#include <string>

#include <rhi.h>

class CommandSignatureManager {
public:
	static CommandSignatureManager& GetInstance();
	void Initialize();
	const rhi::CommandSignature& GetDispatchMeshCommandSignature() {
		return m_dispatchMeshCommandSignature.Get();
	}
	const rhi::CommandSignature& GetDispatchCommandSignature() {
		return m_dispatchCommandSignature.Get();
	}
	const rhi::CommandSignature& GetMaterialEvaluationCommandSignature() {
		return m_materialEvaluationCommandSignature.Get();
	}

private:
	rhi::CommandSignaturePtr m_dispatchMeshCommandSignature;
	rhi::CommandSignaturePtr m_dispatchCommandSignature;
	rhi::CommandSignaturePtr m_materialEvaluationCommandSignature;
};

inline CommandSignatureManager& CommandSignatureManager::GetInstance() {
	static CommandSignatureManager instance;
	return instance;
}