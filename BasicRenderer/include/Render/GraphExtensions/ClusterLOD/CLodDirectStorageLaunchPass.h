#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <tracy/Tracy.hpp>

#include "Interfaces/IResourceResolver.h"
#include "RenderPasses/Base/CopyPass.h"

struct CLodDirectStorageLaunchInputs {
	std::unique_ptr<IResourceResolver> targetSlabResolver;
	std::function<PassReturn()> launchCallback;
	std::function<bool()> hasPendingCallback;
};

class CLodDirectStorageLaunchPass : public CopyPass {
public:
	explicit CLodDirectStorageLaunchPass(CLodDirectStorageLaunchInputs inputs)
		: m_inputs(std::move(inputs)) {}

	void Setup() override {}
	void Cleanup() override {}

	PassReturn Execute(PassExecutionContext&) override {
		ZoneScopedN("CLodDirectStorageLaunchPass::Execute");
		return m_inputs.launchCallback ? m_inputs.launchCallback() : PassReturn{};
	}

	PreparedPass PrepareFrame(FramePreparationContext&) override {
		if (!m_inputs.hasPendingCallback || !m_inputs.hasPendingCallback())
			return PreparedPass::NoOp();
		if (!m_inputs.launchCallback) return {};
		auto result = m_inputs.launchCallback();
		if (result.fence || result.fenceValue) return {};
		return PreparedPass::NoOpWithExternalSignals(
			std::move(result.externalSignalsAfterCompletion));
	}

private:
	void DeclareResourceUsages(CopyPassBuilder* builder) override {
		ZoneScopedN("CLodDirectStorageLaunchPass::DeclareResourceUsages");
		if (m_inputs.targetSlabResolver) {
			builder->WithCopyDest(*m_inputs.targetSlabResolver);
		}
		builder->PreferQueue(QueueKind::Graphics);
	}

	CLodDirectStorageLaunchInputs m_inputs;
};
