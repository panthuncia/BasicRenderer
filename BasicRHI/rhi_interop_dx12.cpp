#include "rhi_dx12.h"
#include "rhi_interop.h"

// Returns non-owning raw pointers. Caller must not store them long-term without AddRef/Release.

namespace rhi {

    bool QueryNativeDevice(Device d, uint32_t iid, void* outStruct, uint32_t outSize) noexcept {
        if (!d.IsValid() || !outStruct) return false;

        auto* impl = static_cast<Dx12Device*>(d.impl);
        if (!impl) return false;

        switch (iid) {
        case RHI_IID_D3D12_DEVICE: {
            if (outSize < sizeof(D3D12DeviceInfo)) return false;

            // Ensure we hand out an ID3D12Device* (not Device10)
            Microsoft::WRL::ComPtr<ID3D12Device> devBase;
            // If you also stored devBase at creation time, you can skip As() and use that ComPtr.
            (void)impl->dev.As(&devBase);

            auto* out = reinterpret_cast<D3D12DeviceInfo*>(outStruct);
            out->device = devBase.Get();            // ID3D12Device*
            out->factory = impl->factory.Get();      // IDXGIFactory7*
            out->adapter = impl->adapter.Get();      // IDXGIAdapter4* (may be null if not stored)
            out->version = 1;
            return true;
        }
        default:
            return false;
        }
    }

    bool QueryNativeQueue(Queue q, uint32_t iid, void* outStruct, uint32_t outSize) noexcept {
        if (!q.IsValid() || !outStruct) return false;
        if (iid != RHI_IID_D3D12_QUEUE) return false;
        if (outSize < sizeof(D3D12QueueInfo)) return false;

        auto* s = static_cast<Dx12QueueState*>(q.impl);
        if (!s || !s->q) return false;

        auto* out = reinterpret_cast<D3D12QueueInfo*>(outStruct);
        out->queue = s->q.Get();   // ID3D12CommandQueue*
        out->version = 1;
        return true;
    }

    bool QueryNativeCmdList(CommandList cl, uint32_t iid, void* outStruct, uint32_t outSize) noexcept {
        if (!cl.IsValid() || !outStruct) return false;
        if (iid != RHI_IID_D3D12_CMD_LIST) return false;
        if (outSize < sizeof(D3D12CmdListInfo)) return false;

        auto* rec = static_cast<Dx12CommandList*>(cl.impl);
        if (!rec || !rec->cl) return false;

        // Hand out ID3D12GraphicsCommandList* (QI from v7 to base)
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> baseCl;
        (void)rec->cl.As(&baseCl);

        auto* out = reinterpret_cast<D3D12CmdListInfo*>(outStruct);
        out->cmdList = baseCl.Get();              // ID3D12GraphicsCommandList*
        out->allocator = rec->alloc.Get();          // ID3D12CommandAllocator* (may be null if not tracked)
        out->version = 1;
        return true;
    }

    bool QueryNativeSwapchain(Swapchain sc, uint32_t iid, void* outStruct, uint32_t outSize) noexcept {
        if (!sc.IsValid() || !outStruct) return false;
        if (iid != RHI_IID_D3D12_SWAPCHAIN) return false;
        if (outSize < sizeof(D3D12SwapchainInfo)) return false;

        auto* s = static_cast<Dx12Swapchain*>(sc.impl);
        if (!s || !s->sc) return false;

        auto* out = reinterpret_cast<D3D12SwapchainInfo*>(outStruct);
        out->swapchain = s->sc.Get(); // IDXGISwapChain3*
        out->version = 1;
        return true;
    }

    bool QueryNativeResource(Resource h, uint32_t iid, void* outStruct, uint32_t outSize) noexcept {
        if (!h.IsValid() || !outStruct) return false;
        if (iid != RHI_IID_D3D12_RESOURCE) return false;
        if (outSize < sizeof(D3D12ResourceInfo)) return false;
		// Cast to Dx12Buffer or Dx12Texture based on h.IsTexture()
		Dx12Texture* texRec = nullptr;
		Dx12Buffer* bufRec = nullptr;
		if (h.IsTexture()) {
			texRec = static_cast<Dx12Texture*>(h.impl);
		}
		else {
			bufRec = static_cast<Dx12Buffer*>(h.impl);
		}

        if (h.IsTexture()) {
            if (!texRec || !texRec->res) return false;
            auto* out = reinterpret_cast<D3D12ResourceInfo*>(outStruct);
            out->resource = texRec->res.Get(); // ID3D12Resource*
            out->version = 1;
            return true;
        }
        else {
            if (!bufRec || !bufRec->res) return false;
            auto* out = reinterpret_cast<D3D12ResourceInfo*>(outStruct);
            out->resource = bufRec->res.Get(); // ID3D12Resource*
            out->version = 1;
            return true;
        }
		return false;
	}

    namespace dx12 {

        bool enable_streamline_interposer(Device d, PFN_UpgradeInterface upgrade) {
            auto* impl = static_cast<Dx12Device*>(d.impl);

            // Upgrade factory
            IDXGIFactory7* fac = impl->factory.Get();
            if (!upgrade(reinterpret_cast<void**>(&fac))) return false;
            impl->slFactory.Attach(fac); // now holds upgraded factory

            // upgrade device to base iface
            IUnknown* dev = impl->dev.Get();
            if (upgrade(reinterpret_cast<void**>(&dev))) {
                Microsoft::WRL::ComPtr<ID3D12Device> base;
                dev->QueryInterface(IID_PPV_ARGS(&base));
                impl->slDeviceBase = std::move(base);
            }

            impl->upgradeFn = upgrade;
            return true;
        }

        void disable_streamline_interposer(Device d) {
            auto* impl = static_cast<Dx12Device*>(d.impl);
            impl->upgradeFn = nullptr;
            impl->slFactory.Reset();
            impl->slDeviceBase.Reset();
        }
    }

}