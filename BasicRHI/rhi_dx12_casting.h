#pragma once
#include "rhi_dx12.h"

namespace rhi::dx12_detail
{
	// Base: get Dx12Device from any wrapper that follows "impl = Dx12Device*".
	template<typename WrapperT>
	[[nodiscard]] inline Dx12Device* Dev(const WrapperT* w) noexcept
	{
		return w ? static_cast<Dx12Device*>(w->impl) : nullptr;
	}

	[[nodiscard]] inline Dx12Device* Dev(const Device* d) noexcept
	{
		return d ? static_cast<Dx12Device*>(d->impl) : nullptr;
	}

	[[nodiscard]] inline Dx12Device* Dev(const DeviceDeletionContext* ctx) noexcept
	{
		return ctx ? static_cast<Dx12Device*>(ctx->impl) : nullptr;
	}

	// Queue state resolution
	[[nodiscard]] inline Dx12QueueState* QState(const Queue* q) noexcept
	{
		Dx12Device* dev = Dev(q);
		if (!dev || !q) return nullptr;

		switch (q->GetKind())
		{
		case QueueKind::Graphics: return &dev->gfx;
		case QueueKind::Compute:  return &dev->comp;
		case QueueKind::Copy:     return &dev->copy;
		default:                  return nullptr;
		}
	}

	// Registry-resolved backend objects.
	[[nodiscard]] inline Dx12Resource* Res(const Resource* r) noexcept
	{
		Dx12Device* dev = Dev(r);
		return (dev && r) ? dev->resources.get(r->GetHandle()) : nullptr;
	}

	[[nodiscard]] inline Dx12Sampler* Samp(const Sampler* s) noexcept
	{
		Dx12Device* dev = Dev(s);
		return (dev && s) ? dev->samplers.get(s->GetHandle()) : nullptr;
	}

	[[nodiscard]] inline Dx12PipelineLayout* PL(const PipelineLayout* p) noexcept
	{
		Dx12Device* dev = Dev(p);
		return (dev && p) ? dev->pipelineLayouts.get(p->GetHandle()) : nullptr;
	}

	[[nodiscard]] inline Dx12Pipeline* Pso(const Pipeline* p) noexcept
	{
		Dx12Device* dev = Dev(p);
		return (dev && p) ? dev->pipelines.get(p->GetHandle()) : nullptr;
	}

	[[nodiscard]] inline Dx12CommandSignature* CSig(const CommandSignature* cs) noexcept
	{
		Dx12Device* dev = Dev(cs);
		return (dev && cs) ? dev->commandSignatures.get(cs->GetHandle()) : nullptr;
	}

	[[nodiscard]] inline Dx12DescriptorHeap* DH(const DescriptorHeap* h) noexcept
	{
		Dx12Device* dev = Dev(h);
		return (dev && h) ? dev->descHeaps.get(h->GetHandle()) : nullptr;
	}

	[[nodiscard]] inline Dx12Allocator* Alloc(const CommandAllocator* a) noexcept
	{
		Dx12Device* dev = Dev(a);
		return (dev && a) ? dev->allocators.get(a->GetHandle()) : nullptr;
	}

	[[nodiscard]] inline Dx12CommandList* CL(const CommandList* cl) noexcept
	{
		Dx12Device* dev = Dev(cl);
		return (dev && cl) ? dev->commandLists.get(cl->GetHandle()) : nullptr;
	}

	[[nodiscard]] inline Dx12Timeline* TL(const Timeline* t) noexcept
	{
		Dx12Device* dev = Dev(t);
		return (dev && t) ? dev->timelines.get(t->GetHandle()) : nullptr;
	}

	[[nodiscard]] inline Dx12Heap* Hp(const Heap* h) noexcept
	{
		Dx12Device* dev = Dev(h);
		return (dev && h) ? dev->heaps.get(h->GetHandle()) : nullptr;
	}

	[[nodiscard]] inline Dx12QueryPool* QP(const QueryPool* qp) noexcept
	{
		Dx12Device* dev = Dev(qp);
		return (dev && qp) ? dev->queryPools.get(qp->GetHandle()) : nullptr;
	}
}