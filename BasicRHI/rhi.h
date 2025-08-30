#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <directx/d3dcommon.h>

#include "resource_states.h"

namespace rhi {

    inline constexpr uint32_t RHI_DEVICE_ABI_MIN = 1;
    inline constexpr uint32_t RHI_QUEUE_ABI_MIN = 1;
    inline constexpr uint32_t RHI_CL_ABI_MIN = 1;
    inline constexpr uint32_t RHI_SC_ABI_MIN = 1;
    inline constexpr uint32_t RHI_CA_ABI_MIN = 1;

    struct Handle32 { uint32_t index{ 0xFFFFFFFFu }; uint32_t generation{ 0 }; constexpr bool valid() const noexcept { return index != 0xFFFFFFFFu; } };
    using ResourceHandle = Handle32;
    using ViewHandle = Handle32;
    using SamplerHandle = Handle32;
    using PipelineHandle = Handle32;
    using CommandSignatureHandle = Handle32;
    using PipelineLayoutHandle = Handle32;
    using DescriptorHeapHandle = Handle32;
	using CommandAllocatorHandle = Handle32;

    //RAII unique smart pointer
    template<typename Tag>
    class HandlePtr {
    public:
        HandlePtr() = default;
        HandlePtr(Device* d, Handle32 h) : dev_(d), h_(h) {}
        ~HandlePtr() { reset(); }

        HandlePtr(const HandlePtr&) = delete;
        HandlePtr& operator=(const HandlePtr&) = delete;

        HandlePtr(HandlePtr&& o) noexcept { swap(o); }
        HandlePtr& operator=(HandlePtr&& o) noexcept { if (this != &o) { reset(); swap(o); } return *this; }

        Handle32 Get() const noexcept { return h_; }
        explicit operator bool() const noexcept { return h_.valid(); }

        Handle32 Release() noexcept { auto t = h_; h_ = {}; dev_ = nullptr; return t; }
        void     Reset()   noexcept { if (dev_ && h_.valid()) Tag::Destroy(dev_, h_); h_ = {}; dev_ = nullptr; }

		// cheap runtime liveness check (needs device) TODO: actually check with device
        bool Valid() const noexcept { return dev_ }

        Device* Device()  const noexcept { return dev_; }

    private:
        void swap(HandlePtr& o) noexcept { std::swap(dev_, o.dev_); std::swap(h_, o.h_); }

        Device* dev_{};
        Handle32 h_{};
    };

    struct ResourceTag {
        static void Destroy(Device* d, Handle32 h) noexcept { d->DestroyBuffer(ResourceHandle{ h.index,h.generation }); }
    };

	// Anything that is not trivially destructible needs its own tag with Destroy()
	using ResourcePtr = HandlePtr<ResourceTag>;
	using SamplerPtr = HandlePtr<struct SamplerTag>;
	using PipelinePtr = HandlePtr<struct PipelineTag>;
	using CommandSignaturePtr = HandlePtr<struct CommandSignatureTag>;
	using PipelineLayoutPtr = HandlePtr<struct PipelineLayoutTag>;
	using DescriptorHeapPtr = HandlePtr<struct DescriptorHeapTag>;
	using CommandAllocatorPtr = HandlePtr<struct CommandAllocatorTag>;

	// ---------------- Enums & structs ----------------

    enum class Backend : uint32_t { Null, D3D12, Vulkan };
    enum class QueueKind : uint32_t { Graphics, Compute, Copy };

    enum class Result : uint32_t { Ok, Failed, Unsupported, OutOfMemory, InvalidArg, DeviceLost };

    template<typename T>
    struct Span {
        const T* data{};
        uint32_t size{};
        const T* begin() const noexcept { return data; }
        const T* end()   const noexcept { return data + size; }
    };

    // ---------------- Formats & resource descriptors (trimmed) ----------------

    enum class Format : uint32_t { 
        Unknown,
		R32G32B32A32_Typeless, R32G32B32A32_Float, R32G32B32A32_UInt, R32G32B32A32_SInt,
		R32G32B32_Typeless, R32G32B32_Float, R32G32B32_UInt, R32G32B32_SInt,
		R16G16B16A16_Typeless, R16G16B16A16_Float, R16G16B16A16_UNorm, R16G16B16A16_UInt, R16G16B16A16_SNorm, R16G16B16A16_SInt,
		R32G32_Typeless, R32G32_Float, R32G32_UInt, R32G32_SInt,
		R10G10B10A2_Typeless, R10G10B10,
		R11G11B10_Float,
		R8G8B8A8_Typeless, R8G8B8A8_UNorm, R8G8B8A8_UNorm_sRGB, R8G8B8A8_UInt, R8G8B8A8_SNorm, R8G8B8A8_SInt,
		R16G16_Typeless, R16G16_Float, R16G16_UNorm, R16G16_UInt, R16G16_SNorm, R16G16_SInt,
		R32_Typeless, D32_Float, R32_Float, R32_UInt, R32_SInt,
		R8G8_Typeless, R8G8_UNorm, R8G8_UInt, R8G8_SNorm, R8G8_SInt,
		R16_Typeless, R16_Float, R16_UNorm, R16_UInt, R16_SNorm, R16_SInt,
		R8_Typeless, R8_UNorm, R8_UInt, R8_SNorm, R8_SInt,
		BC1_Typeless, BC1_UNorm, BC1_UNorm_sRGB,
		BC2_Typeless, BC2_UNorm, BC2_UNorm_sRGB,
		BC3_Typeless, BC3_UNorm, BC3_UNorm_sRGB,
		BC4_Typeless, BC4_UNorm, BC4_SNorm,
		BC5_Typeless, BC5_UNorm, BC5_SNorm,
		BC6H_Typeless, BC6H_UF16, BC6H_SF16,
		BC7_Typeless, BC7_UNorm, BC7_UNorm_sRGB,
    };

    enum class Memory : uint32_t { DeviceLocal, Upload, Readback };

    enum class ViewKind : uint32_t { SRV, UAV, RTV, DSV };
    struct TextureSubresourceRange { uint32_t baseMip = 0, mipCount = 1; uint32_t baseLayer = 0, layerCount = 1; };
    struct ViewDesc { ViewKind kind = ViewKind::SRV; ResourceHandle texture{}; TextureSubresourceRange range{}; Format formatOverride = Format::Unknown; };

    struct SamplerDesc { uint32_t maxAniso = 1; };

    enum class IndirectArgKind : uint32_t {
        Draw,           // D3D12_INDIRECT_ARGUMENT_TYPE_DRAW
        DrawIndexed,    // D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED
        Dispatch,       // D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH
        VertexBuffer,   // D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW
        IndexBuffer,    // D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW
		Constant,       // D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT
		ConstantBuffer, // D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW
		ShaderResource, // D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW
		UnorderedAccess,// D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW
		DispatchRays,  // DXR: D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS
		DispatchMesh,  // Mesh shaders: D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH
		IncrementingConstant, // D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT
    };

    struct IndirectArg {
        IndirectArgKind kind{};
        union {
            struct { uint32_t slot; } vertexBuffer; // for VertexBuffer
            struct { uint32_t rootIndex; uint32_t destOffset32; uint32_t num32; } rootConstants; // RootConstants
            // others need no extra fields
        } u{};
    };

    enum class ShaderStage : uint32_t { Vertex = 1, Pixel = 2, Compute = 4, Mesh = 8, Task = 16, All = 0xFFFFFFFFu };

    struct LayoutBindingRange { uint32_t set = 0, binding = 0, count = 1; bool readOnly = true; ShaderStage visibility = ShaderStage::All; };

    struct PushConstantRangeDesc {
        ShaderStage visibility = ShaderStage::All;
        uint32_t num32BitValues = 0;
        uint32_t set = 0;            // maps to RegisterSpace on DX12 (ignored on Vulkan)
        uint32_t binding = 0;        // maps to ShaderRegister on DX12 (ignored on Vulkan)
    };

    struct StaticSamplerDesc {
        SamplerDesc sampler;
        ShaderStage visibility = ShaderStage::All;
        uint32_t set = 0;            // DX12 RegisterSpace / Vulkan set index
        uint32_t binding = 0;        // DX12 ShaderRegister / Vulkan binding
        uint32_t arrayCount = 1;
    };

    enum PipelineLayoutFlags : uint32_t { None = 0, AllowInputAssembler = 1 << 0 }; // only for graphics pipelines

    struct PipelineLayoutDesc {
        Span<LayoutBindingRange> ranges{};
        Span<PushConstantRangeDesc> pushConstants{};
        Span<StaticSamplerDesc> staticSamplers{};
        PipelineLayoutFlags flags;
    };

    struct CommandSignatureDesc {
        Span<IndirectArg> args;
        uint32_t byteStride = 0; // sizeof(struct in argument buffer)
    };

	enum class DescriptorHeapType : uint32_t { CbvSrvUav, Sampler, RTV, DSV };

    struct DescriptorHeapDesc {
        DescriptorHeapType type;      // CbvSrvUav, Sampler, RTV, DSV
        uint32_t           capacity;  // NumDescriptors
        bool               shaderVisible;
        const char* debugName{ nullptr };
    };

    struct DescriptorSlot { DescriptorHeapHandle heap{}; uint32_t index{}; };

    enum class TextureViewDim : uint32_t { Tex2D, Tex2DArray, Tex3D, Cube, CubeArray };

    enum class BufferViewKind : uint32_t { Raw, Structured, Typed };

	enum class ViewType : uint32_t { Undefined, Texture, Buffer };

    struct SrvDesc {
        // Choose buffer OR texture
        ViewType             type{ ViewType::Undefined };
        ResourceHandle  resource{};
        // Texture path
        TextureViewDim        texDim{ TextureViewDim::Tex2D };
        TextureSubresourceRange texRange{};
        Format                texFormatOverride{ Format::Unknown }; // Unknown => use texture format
        // Buffer path
        BufferViewKind bufKind{ BufferViewKind::Raw };
        Format         bufFormat{ Format::Unknown };    // Typed only
        uint64_t       firstElement{ 0 };               // 32-bit units for RAW; elements for others
        uint32_t       numElements{ 0 };
        uint32_t       structureByteStride{ 0 };        // Structured only
    };

    struct UavDesc {
		ViewType type{ ViewType::Undefined };
        ResourceHandle  resource{};
        // Texture path
        TextureViewDim        texDim{ TextureViewDim::Tex2D };
        TextureSubresourceRange texRange{};
        Format                texFormatOverride{ Format::Unknown };
        // Buffer path
        BufferViewKind bufKind{ BufferViewKind::Raw };
        Format         bufFormat{ Format::Unknown };
        uint64_t       firstElement{ 0 };
        uint32_t       numElements{ 0 };
        uint32_t       structureByteStride{ 0 };
        uint32_t       counterOffsetBytes{ 0 };
    };

    struct CbvDesc { uint64_t byteOffset = 0; uint32_t byteSize = 0; /* 256B aligned */ };

    // RTV/DSV descriptions (texture-only)
    struct RtvDesc {
        ResourceHandle       texture{};
        TextureViewDim       dim{ TextureViewDim::Tex2D };
        TextureSubresourceRange range{};
        Format              formatOverride{ Format::Unknown };
    };
    struct DsvDesc {
        ResourceHandle       texture{};
        TextureViewDim       dim{ TextureViewDim::Tex2D };
        TextureSubresourceRange range{};
        Format              formatOverride{ Format::Unknown };
        bool readOnlyDepth{ false };
        bool readOnlyStencil{ true };
    };

    // Command list binding for shader-visible heaps
    struct BoundDescriptorHeaps {
        DescriptorHeapHandle cbvSrvUav{};
        DescriptorHeapHandle sampler{};
    };

    enum class FillMode : uint32_t { Solid, Wireframe };
    enum class CullMode : uint32_t { None, Front, Back };
    enum class CompareOp : uint32_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
    enum class BlendFactor : uint32_t { One, Zero, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DstColor, InvDstColor, DstAlpha, InvDstAlpha };
    enum class BlendOp : uint32_t { Add, Sub, RevSub, Min, Max };
	enum ColorWriteEnable : uint8_t { R = 1, G = 2, B = 4, A = 8, All = 0x0F };

    struct ShaderBinary { const void* data{}; uint32_t size{}; };
    struct RasterState {
        FillMode fill = FillMode::Solid;
        CullMode cull = CullMode::Back;
        bool frontCCW = false;
        int32_t depthBias = 0;
        float   depthBiasClamp = 0;
        float   slopeScaledDepthBias = 0;
        bool    conservative = false;
    };

    struct DepthStencilState {
        bool depthEnable = true;
        bool depthWrite = true;
        CompareOp depthFunc = CompareOp::LessEqual;
        // (Stencil omitted here; add if you need it)
    };

    struct BlendAttachment {
        bool enable = false;
        BlendFactor srcColor = BlendFactor::One;
        BlendFactor dstColor = BlendFactor::Zero;
        BlendOp     colorOp = BlendOp::Add;
        BlendFactor srcAlpha = BlendFactor::One;
        BlendFactor dstAlpha = BlendFactor::Zero;
        BlendOp     alphaOp = BlendOp::Add;
        ColorWriteEnable     writeMask = ColorWriteEnable::All; // RGBA
    };
    struct BlendState {
		bool alphaToCoverage = false;
		bool independentBlend = false;
        uint32_t numAttachments = 1;
        BlendAttachment attachments[8]{};
    };

    struct RenderTargets {
        uint32_t count = 0;
        Format   formats[8]{ Format::Unknown };
    };
    struct SampleDesc { uint32_t count = 1; uint32_t quality = 0; };

    enum class PsoSubobj : uint32_t {
        Layout,
        Shader,           // AS/MS/VS/PS -> stage inside payload
        Rasterizer,
        Blend,
        DepthStencil,
        RTVFormats,
        DSVFormat,
        Sample,
        Flags             // optional backend-specific flags bitset
    };

    struct SubobjLayout { PipelineLayoutHandle layout{}; };
    struct SubobjShader { ShaderStage stage{}; ShaderBinary bytecode{}; };
    struct SubobjRaster { RasterState rs{}; };
    struct SubobjBlend { BlendState bs{}; };
    struct SubobjDepth { DepthStencilState ds{}; };
    struct SubobjRTVs { RenderTargets rt{}; };
    struct SubobjDSV { Format dsv = Format::Unknown; };
    struct SubobjSample { SampleDesc sd{}; };
    // struct SubobjFlags { uint64_t mask = 0; }; // optional

    struct PipelineStreamItem {
        PsoSubobj type;
        const void* data;   // points to one of the Subobj* structs above
        uint32_t   size;    // sizeof(that struct)
    };

    // Convenience makers
    inline PipelineStreamItem Make(const SubobjLayout& x) { return { PsoSubobj::Layout, &x,  sizeof(x) }; }
    inline PipelineStreamItem Make(const SubobjShader& x) { return { PsoSubobj::Shader, &x,  sizeof(x) }; }
    inline PipelineStreamItem Make(const SubobjRaster& x) { return { PsoSubobj::Rasterizer, &x, sizeof(x) }; }
    inline PipelineStreamItem Make(const SubobjBlend& x) { return { PsoSubobj::Blend, &x,   sizeof(x) }; }
    inline PipelineStreamItem Make(const SubobjDepth& x) { return { PsoSubobj::DepthStencil, &x, sizeof(x) }; }
    inline PipelineStreamItem Make(const SubobjRTVs& x) { return { PsoSubobj::RTVFormats, &x, sizeof(x) }; }
    inline PipelineStreamItem Make(const SubobjDSV& x) { return { PsoSubobj::DSVFormat, &x, sizeof(x) }; }
    inline PipelineStreamItem Make(const SubobjSample& x) { return { PsoSubobj::Sample, &x,  sizeof(x) }; }


    // ---------------- Pass & barriers (minimal) ----------------

    enum class LoadOp : uint32_t { Load, Clear, DontCare }; enum class StoreOp : uint32_t { Store, DontCare };
    struct ClearValue { float rgba[4]{ 0,0,0,1 }; float depth = 1.0f; uint8_t stencil = 0; };
    struct ColorAttachment {
        DescriptorSlot rtv{};
        LoadOp loadOp = LoadOp::Load;
        StoreOp storeOp = StoreOp::Store;
        ClearValue clear{};
    };

    struct DepthAttachment {
        DescriptorSlot dsv{};
        LoadOp  depthLoad = LoadOp::Load;
        StoreOp depthStore = StoreOp::Store;
        LoadOp  stencilLoad = LoadOp::DontCare;
        StoreOp stencilStore = StoreOp::DontCare;
        ClearValue clear{};
        bool readOnly = false;
    };

    struct PassBeginInfo {
        Span<ColorAttachment> colors{};
        const DepthAttachment* depth{};
        uint32_t width = 0, height = 0;
        const char* debugName = nullptr;
    };
    enum ResourceFlags : uint32_t {
        None = 0,
        AllowRenderTarget = 1 << 0,
        AllowDepthStencil = 1 << 1,
        AllowUnorderedAccess = 1 << 2,
        DenyShaderResource = 1 << 3,
        AllowCrossAdapter = 1 << 4,
        AllowSimultaneousAccess = 1 << 5,
        VideoDecodeReferenceOnly = 1 << 6,
        VideoEncodeReferenceOnly = 1 << 7,
        RaytracingAccelerationStructure = 1 << 8,
    };
    struct BufferDesc {
        uint64_t     sizeBytes = 0;
        Memory       memory = Memory::DeviceLocal;
    };
    struct TextureDesc {
        Format       format = Format::Unknown;
        uint32_t     width = 1;
        uint32_t     height = 1;
        uint16_t     depthOrLayers = 1;     // depth for 3D, arraySize otherwise
        uint16_t     mipLevels = 1;
        TextureViewDim dim = TextureViewDim::Tex2D;
        ResourceLayout initialLayout = ResourceLayout::Undefined;
        const ClearValue* optimizedClear = nullptr;    // optional, if RTV/DSV
    };
	enum class ResourceType : uint32_t { Unknown, Texture, Buffer };
    struct ResourceDesc { 
        ResourceType type = ResourceType::Unknown; 
        ResourceFlags flags;
        const char* debugName = nullptr;
        union { 
            TextureDesc texture; 
            BufferDesc buffer; 
        }; 
    };

    struct VertexBufferView { ResourceHandle buffer{}; uint64_t offset = 0; uint32_t sizeBytes = 0; uint32_t stride = 0; };

    enum class Stage : uint32_t { Top, Draw, Pixel, Compute, Copy, Bottom }; 

    // ---------------- Submission & timelines (thin) ----------------
    struct Timeline { Handle32 id{}; }; struct TimelinePoint { Timeline t; uint64_t value = 0; };
    struct SubmitDesc { Span<TimelinePoint> waits{}; Span<TimelinePoint> signals{}; };

    // --- Texture / Buffer / Global barrier descriptions ---
    struct TextureBarrier {
        ResourceHandle        texture{};
        TextureSubresourceRange range{}; // baseMip/mipCount, baseLayer/layerCount
        ResourceSyncState    beforeSync{ ResourceSyncState::None };
        ResourceSyncState    afterSync{ ResourceSyncState::None };
        ResourceAccessType   beforeAccess{ ResourceAccessType::None };
        ResourceAccessType   afterAccess{ ResourceAccessType::None };
        ResourceLayout       beforeLayout{ ResourceLayout::Undefined };
        ResourceLayout       afterLayout{ ResourceLayout::Undefined };
        bool                 discard{ false }; // optional hint; can be ignored if backend doesn’t use it
    };

    struct BufferBarrier {
        ResourceHandle       buffer{};
        uint64_t             offset{ 0 };
        uint64_t             size{ ~0ull }; // ~0 -> whole resource from offset
        ResourceSyncState    beforeSync{ ResourceSyncState::None };
        ResourceSyncState    afterSync{ ResourceSyncState::None };
        ResourceAccessType   beforeAccess{ ResourceAccessType::None };
        ResourceAccessType   afterAccess{ ResourceAccessType::None };
    };

    struct GlobalBarrier {
        ResourceSyncState    beforeSync{ ResourceSyncState::None };
        ResourceSyncState    afterSync{ ResourceSyncState::None };
        ResourceAccessType   beforeAccess{ ResourceAccessType::None };
        ResourceAccessType   afterAccess{ ResourceAccessType::None };
    };

    // Batch to submit in one call
    struct BarrierBatch {
        Span<TextureBarrier> textures{};
        Span<BufferBarrier>  buffers{};
        Span<GlobalBarrier>  globals{};
    };


    // ---------------- POD wrappers + VTables ----------------
    struct Device;       struct Queue;       struct CommandList;       struct Swapchain;       struct CommandAllocator;
    struct DeviceVTable; struct QueueVTable; struct CommandListVTable; struct SwapchainVTable; struct CommandAllocatorVTable;

    struct Queue { 
        void* impl{}; 
        const QueueVTable* vt{}; 
        explicit constexpr operator bool() const noexcept {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_QUEUE_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
        inline Result Submit(Span<CommandList> lists, const SubmitDesc& s) noexcept;
        inline Result Signal(const TimelinePoint& p) noexcept;
        inline Result Wait(const TimelinePoint& p) noexcept;
    };

    struct CommandList { 
        void* impl{}; 
        const CommandListVTable* vt{}; 
        explicit constexpr operator bool() const noexcept {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_CL_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
        void Begin(const char* name = nullptr) noexcept;
        void End() noexcept;
		void Recycle(CommandAllocator& ca) noexcept;
        void BeginPass(const PassBeginInfo& p) noexcept;
        void EndPass() noexcept;
        void Barriers(const BarrierBatch& b) noexcept;
        void BindLayout(PipelineLayoutHandle l) noexcept;
        void BindPipeline(PipelineHandle p) noexcept;
        void SetVertexBuffers(uint32_t startSlot, uint32_t numViews, VertexBufferView* pBufferViews);
        void SetIndexBuffer(ResourceHandle b, uint64_t offset, uint32_t sizeBytes, bool idx32);
        void Draw(uint32_t v, uint32_t i, uint32_t fv, uint32_t fi) noexcept;
        void DrawIndexed(uint32_t i, uint32_t inst, uint32_t firstIdx, int32_t vOff, uint32_t firstI) noexcept;
        void Dispatch(uint32_t x, uint32_t y, uint32_t z) noexcept;
        void ClearView(ViewHandle v, const ClearValue& c) noexcept;
        void ExecuteIndirect(CommandSignatureHandle sig,
            ResourceHandle argBuf, uint64_t argOff,
            ResourceHandle cntBuf, uint64_t cntOff,
            uint32_t maxCount) noexcept;
		void SetDescriptorHeaps(DescriptorHeapHandle cbvSrvUav, DescriptorHeapHandle samp) noexcept;
        void Barrier(const BarrierBatch& b) noexcept;
    };

    struct Swapchain { 
        void* impl{}; 
        const SwapchainVTable* vt{}; 
        explicit constexpr operator bool() const noexcept {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_SC_ABI_MIN;
        }
        constexpr bool isValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void reset() noexcept { impl = nullptr; vt = nullptr; }
        inline uint32_t ImageCount() noexcept;
        inline uint32_t CurrentImageIndex() noexcept;
        inline ViewHandle RTV(uint32_t i) noexcept;
        inline ResourcePtr Image(uint32_t i) noexcept;
        inline Result Resize(uint32_t w, uint32_t h) noexcept;
        inline Result Present(bool vsync) noexcept;
    };

    struct DeviceVTable {
        PipelinePtr(*createPipelineFromStream)(Device*, const PipelineStreamItem* items, uint32_t count) noexcept;
        PipelineLayoutPtr(*createPipelineLayout)(Device*, const PipelineLayoutDesc&) noexcept;
        CommandSignaturePtr(*createCommandSignature)(Device*, const CommandSignatureDesc&, PipelineLayoutHandle /*layoutOrNull*/) noexcept;
        CommandAllocator(*createCommandAllocator)(Device*, QueueKind) noexcept;
        CommandList(*createCommandList)(Device*, QueueKind, CommandAllocator) noexcept;
        Swapchain(*createSwapchain)(Device*, void* hwnd, uint32_t w, uint32_t h, Format fmt, uint32_t bufferCount, bool allowTearing) noexcept;
        DescriptorHeap(*createDescriptorHeap)(Device*, const DescriptorHeapDesc&) noexcept;

        Result(*createConstantBufferView)(Device*, DescriptorSlot dst, ResourceHandle, const CbvDesc&) noexcept;
        Result(*createShaderResourceView)(Device*, DescriptorSlot dst, const SrvDesc&) noexcept;
        Result(*createUnorderedAccessView)(Device*, DescriptorSlot dst, const UavDesc&) noexcept;
        Result(*createRenderTargetView)(Device*, DescriptorSlot dst, const RtvDesc&) noexcept;
        Result(*createDepthStencilView)(Device*, DescriptorSlot dst, const DsvDesc&) noexcept;
        Result(*createSampler)(Device*, DescriptorSlot dst, const SamplerDesc&) noexcept;
        ResourcePtr(*createCommittedResource)(Device*, const ResourceDesc&) noexcept;

        void (*destroySampler)(Device*, SamplerHandle) noexcept;
        void (*destroyPipelineLayout)(Device*, PipelineLayoutHandle) noexcept;
        void (*destroyPipeline)(Device*, PipelineHandle) noexcept;
        void (*destroyCommandSignature)(Device*, CommandSignatureHandle) noexcept;
        void (*destroyCommandAllocator)(Device*, CommandAllocator*) noexcept;
        void (*destroyCommandList)(Device*, CommandList*) noexcept;
        void (*destroySwapchain)(Device*, Swapchain*) noexcept;
        void (*destroyDescriptorHeap)(Device*, DescriptorHeapHandle) noexcept;
        void (*destroyBuffer)(Device*, ResourceHandle) noexcept;
        void (*destroyTexture)(Device*, ResourceHandle) noexcept;

        Queue(*getQueue)(Device*, QueueKind) noexcept;
        Result(*deviceWaitIdle)(Device*) noexcept;
        void (*flushDeletionQueue)(Device*) noexcept;
        uint32_t(*getDescriptorHandleIncrementSize)(Device*, DescriptorHeapType) noexcept;

        void (*destroyDevice)(Device*) noexcept;
        uint32_t abi_version = 1;
    };

    struct QueueVTable { Result(*submit)(Queue*, Span<CommandList>, const SubmitDesc&) noexcept; Result(*signal)(Queue*, const TimelinePoint&) noexcept; Result(*wait)(Queue*, const TimelinePoint&) noexcept; uint32_t abi_version = 1; };

    struct CommandAllocator {
        void* impl{}; // backend wrap (owns Handle32)
        const CommandAllocatorVTable* vt{}; // vtable
        explicit constexpr operator bool() const noexcept {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_CA_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; } // Naming conflict with vt->reset
        inline void Recycle() noexcept { vt->reset(this); } // GPU-side reset (allocator->Reset)
    };

    struct CommandAllocatorVTable {
        void (*reset)(CommandAllocator*) noexcept;  // allocator Reset()
        uint32_t abi_version = 1;
    };

    struct CommandListVTable {
        void (*begin)(CommandList*, const char* debugName) noexcept;
        void (*end)(CommandList*) noexcept;
		void (*recycle)(CommandList*, CommandAllocator& alloc) noexcept;
        void (*beginPass)(CommandList*, const PassBeginInfo&) noexcept;
        void (*endPass)(CommandList*) noexcept;
        void (*barriers)(CommandList*, const BarrierBatch&) noexcept;
        void (*bindLayout)(CommandList*, PipelineLayoutHandle) noexcept;
        void (*bindPipeline)(CommandList*, PipelineHandle) noexcept;
        void (*setVertexBuffers)(CommandList*, uint32_t startSlot, uint32_t numViews, VertexBufferView* pBufferViews) noexcept;
        void (*setIndexBuffer)(CommandList*, ResourceHandle b, uint64_t offset, uint32_t sizeBytes, bool idx32) noexcept;
        void (*draw)(CommandList*, uint32_t vtxCount, uint32_t instCount, uint32_t firstVtx, uint32_t firstInst) noexcept;
        void (*drawIndexed)(CommandList*, uint32_t idxCount, uint32_t instCount, uint32_t firstIdx, int32_t vtxOffset, uint32_t firstInst) noexcept;
        void (*dispatch)(CommandList*, uint32_t x, uint32_t y, uint32_t z) noexcept;
        void (*clearView)(CommandList*, ViewHandle, const ClearValue&) noexcept;
        void (*executeIndirect)(CommandList*,
            CommandSignatureHandle sig,
            ResourceHandle argumentBuffer, uint64_t argumentOffset,
            ResourceHandle countBuffer, uint64_t countOffset,
            uint32_t   maxCommandCount) noexcept;
        void (*setDescriptorHeaps)(CommandList*, DescriptorHeapHandle cbvSrvUav, DescriptorHeapHandle sampler) noexcept;
        void (*barriers)(CommandList*, const BarrierBatch&) noexcept;
        uint32_t abi_version = 1;
    };

    struct SwapchainVTable {
        uint32_t(*imageCount)(Swapchain*) noexcept;
        uint32_t(*currentImageIndex)(Swapchain*) noexcept;
        ViewHandle(*rtv)(Swapchain*, uint32_t img) noexcept; // RTV per image
        ResourcePtr(*image)(Swapchain*, uint32_t img) noexcept; // texture handle per image
        Result(*resize)(Swapchain*, uint32_t w, uint32_t h) noexcept;
        Result(*present)(Swapchain*, bool vsync) noexcept; // Present
        uint32_t abi_version = 1;
    };

    struct Device {
        void* impl{}; 
        const DeviceVTable* vt{};
        explicit constexpr operator bool() const noexcept {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_DEVICE_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
        inline PipelineHandle CreatePipeline(const PipelineStreamItem* items, uint32_t count) noexcept { return vt->createPipelineFromStream(this, items, count); }
        inline CommandList CreateCommandList(QueueKind q, CommandAllocator alloc) noexcept { return vt->createCommandList(this, q, alloc); }
        inline void DestroyCommandList(CommandList* cl) noexcept { vt->destroyCommandList(this, cl); }
        inline Queue GetQueue(QueueKind q) noexcept { return vt->getQueue(this, q); }
        inline Result WaitIdle() noexcept { return vt->deviceWaitIdle(this); }
        inline void FlushDeletionQueue() noexcept { vt->flushDeletionQueue(this); }
        inline Swapchain CreateSwapchain(void* hwnd, uint32_t w, uint32_t h, Format fmt, uint32_t buffers, bool allowTearing) noexcept { return vt->createSwapchain(this, hwnd, w, h, fmt, buffers, allowTearing); }
        inline void DestroySwapchain(Swapchain* sc) noexcept { vt->destroySwapchain(this, sc); }
        inline PipelineLayoutPtr CreatePipelineLayout(const PipelineLayoutDesc& d) noexcept { return vt->createPipelineLayout(this, d); }
        inline void DestroyPipelineLayout(PipelineLayoutHandle h) noexcept { vt->destroyPipelineLayout(this, h); }
        inline CommandSignaturePtr CreateCommandSignature(const CommandSignatureDesc& d, PipelineLayoutHandle layout) noexcept { return vt->createCommandSignature(this, d, layout); }
        inline void DestroyCommandSignature(CommandSignatureHandle h) noexcept { vt->destroyCommandSignature(this, h); }
		inline DescriptorHeap CreateDescriptorHeap(const DescriptorHeapDesc& d) noexcept { return vt->createDescriptorHeap(this, d); }
		inline void DestroyDescriptorHeap(DescriptorHeapHandle h) noexcept { vt->destroyDescriptorHeap(this, h); }
        inline Result CreateConstantBufferView(DescriptorSlot s, ResourceHandle b, const CbvDesc& d) noexcept { return vt->createConstantBufferView(this, s, b, d); }
        inline Result CreateShaderResourceView(DescriptorSlot s, const SrvDesc& d) noexcept { return vt->createShaderResourceView(this, s, d); }
        inline Result CreateUnorderedAccessView(DescriptorSlot s, const UavDesc& d) noexcept { return vt->createUnorderedAccessView(this, s, d); }
        inline Result CreateSampler(DescriptorSlot s, const SamplerDesc& d) noexcept { return vt->createSampler(this, s, d); }
        inline Result CreateRenderTargetView(DescriptorSlot s, const RtvDesc& d) noexcept { return vt->createRenderTargetView(this, s, d); }
        inline Result CreateDepthStencilView(DescriptorSlot s, const DsvDesc& d) noexcept { return vt->createDepthStencilView(this, s, d); }
        inline CommandAllocator CreateCommandAllocator(QueueKind q) noexcept { return vt->createCommandAllocator(this, q); }
        inline CommandList CreateCommandList(QueueKind q, CommandAllocator a) noexcept { return vt->createCommandList(this, q, a); }
		inline ResourcePtr CreateCommittedResource(const ResourceDesc& d) noexcept { return vt->createCommittedResource(this, d); }
        inline void DestroySampler(SamplerHandle h) noexcept { vt->destroySampler(this, h); }
		inline void DestroyPipeline(PipelineHandle h) noexcept { vt->destroyPipeline(this, h); }
		inline void DestroyBuffer(ResourceHandle h) noexcept { vt->destroyBuffer(this, h); }
		inline void DestroyTexture(ResourceHandle h) noexcept { vt->destroyTexture(this, h); }
		inline uint32_t GetDescriptorHandleIncrementSize(DescriptorHeapType t) noexcept { return vt->getDescriptorHandleIncrementSize(this, t); }
        inline void Destroy() noexcept { vt->destroyDevice(this); impl = nullptr; vt = nullptr; }

    };

    inline Result Queue::Submit(Span<CommandList> lists, const SubmitDesc& s) noexcept { return vt->submit(this, lists, s); }
    inline Result Queue::Signal(const TimelinePoint& p) noexcept { return vt->signal(this, p); }
    inline Result Queue::Wait(const TimelinePoint& p) noexcept { return vt->wait(this, p); }

    inline void CommandList::Begin(const char* name) noexcept { vt->begin(this, name); }
    inline void CommandList::End() noexcept { vt->end(this); }
	inline void CommandList::Recycle(CommandAllocator& alloc) noexcept { vt->recycle(this, alloc); }
    inline void CommandList::BeginPass(const PassBeginInfo& p) noexcept { vt->beginPass(this, p); }
    inline void CommandList::EndPass() noexcept { vt->endPass(this); }
    inline void CommandList::Barriers(const BarrierBatch& b) noexcept { vt->barriers(this, b); }
    inline void CommandList::BindLayout(PipelineLayoutHandle l) noexcept { vt->bindLayout(this, l); }
    inline void CommandList::BindPipeline(PipelineHandle p) noexcept { vt->bindPipeline(this, p); }
    inline void CommandList::SetVertexBuffers(uint32_t startSlot, uint32_t numViews, VertexBufferView* pBufferViews) noexcept { vt->setVertexBuffers(this, startSlot, numViews, pBufferViews); }
    inline void CommandList::SetIndexBuffer(ResourceHandle b, uint64_t offset, uint32_t sizeBytes, bool idx32) noexcept { vt->setIndexBuffer(this, b, offset, sizeBytes, idx32); }
    inline void CommandList::Draw(uint32_t v, uint32_t i, uint32_t fv, uint32_t fi) noexcept { vt->draw(this, v, i, fv, fi); }
    inline void CommandList::DrawIndexed(uint32_t i, uint32_t inst, uint32_t firstIdx, int32_t vOff, uint32_t firstI) noexcept { vt->drawIndexed(this, i, inst, firstIdx, vOff, firstI); }
    inline void CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) noexcept { vt->dispatch(this, x, y, z); }
    inline void CommandList::ClearView(ViewHandle v, const ClearValue& c) noexcept { vt->clearView(this, v, c); }
    inline void CommandList::ExecuteIndirect(CommandSignatureHandle sig,
        ResourceHandle argBuf, uint64_t argOff,
        ResourceHandle cntBuf, uint64_t cntOff,
        uint32_t maxCount) noexcept
    { vt->executeIndirect(this, sig, argBuf, argOff, cntBuf, cntOff, maxCount); }
    inline void CommandList::SetDescriptorHeaps(DescriptorHeapHandle csu, DescriptorHeapHandle samp) noexcept { vt->setDescriptorHeaps(this, csu, samp); }
	inline void CommandList::Barrier(const BarrierBatch& b) noexcept { vt->barriers(this, b); }

    inline uint32_t Swapchain::ImageCount() noexcept { return vt->imageCount(this); }
    inline uint32_t Swapchain::CurrentImageIndex() noexcept { return vt->currentImageIndex(this); }
    inline ViewHandle Swapchain::RTV(uint32_t i) noexcept { return vt->rtv(this, i); }
    inline ResourcePtr Swapchain::Image(uint32_t i) noexcept { return vt->image(this, i); }
    inline Result Swapchain::Resize(uint32_t w, uint32_t h) noexcept { return vt->resize(this, w, h); }
    inline Result Swapchain::Present(bool vsync) noexcept { return vt->present(this, vsync); }

    struct DeviceCreateInfo { Backend backend = Backend::D3D12; uint32_t framesInFlight = 3; bool enableDebug = true; };

    Device CreateD3D12Device(const DeviceCreateInfo& ci) noexcept; // implemented in rhi_dx12.cpp

    static inline ShaderBinary DXIL(ID3DBlob* blob) {
        return { blob ? blob->GetBufferPointer() : nullptr,
                 blob ? static_cast<uint32_t>(blob->GetBufferSize()) : 0u };
    }

}