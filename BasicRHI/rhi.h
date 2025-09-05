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
	inline constexpr uint32_t RHI_RESOURCE_ABI_MIN = 1;
	inline constexpr uint32_t RHI_HEAP_ABI_MIN = 1;
    inline constexpr uint32_t RHI_QUERYPOOL_ABI_MIN = 1;
	inline constexpr uint32_t RHI_PIPELINE_ABI_MIN = 1;
	inline constexpr uint32_t RHI_PIPELINELAYOUT_ABI_MIN = 1;
	inline constexpr uint32_t RHI_COMMANDSIGNATURE_ABI_MIN = 1;
	inline constexpr uint32_t RHI_SAMPLER_ABI_MIN = 1;
	inline constexpr uint32_t RHI_DESCRIPTORHEAP_ABI_MIN = 1;
	inline constexpr uint32_t RHI_TIMELINE_ABI_MIN = 1;

    class Device;

    namespace detail {
        // tag types for each handle family
        struct HResource {}; // "either buffer or texture" handle
        struct HView {};
        struct HSampler {};
        struct HPipeline {};
        struct HCmdSig {};
        struct HPipelineLayout {};
        struct HDescHeap {};
        struct HCmdAlloc {};
        struct HTimeline {};
        struct HCommandAllocator {};
        struct HCommandList {};
        struct HHeap {};
        struct HQueryPool {};

        // forward-declared trait; default is no-op (safe in release)
        template<class Tag> struct NameOps {
            static inline void Set(Device*, uint32_t /*index*/, uint32_t /*gen*/, const char*) noexcept {}
        };
    } // namespace detail

    template<class Tag>
    struct Handle {
        uint32_t index{ 0xFFFFFFFFu };
        uint32_t generation{ 0 };
        constexpr bool valid() const noexcept { return index != 0xFFFFFFFFu; }
    };

    using ResourceHandle = Handle<detail::HResource>;
    using ViewHandle = Handle<detail::HView>;
    using SamplerHandle = Handle<detail::HSampler>;
    using PipelineHandle = Handle<detail::HPipeline>;
    using CommandSignatureHandle = Handle<detail::HCmdSig>;
    using PipelineLayoutHandle = Handle<detail::HPipelineLayout>;
    using DescriptorHeapHandle = Handle<detail::HDescHeap>;
    using TimelineHandle = Handle<detail::HTimeline>;
    using CommandAllocatorHandle = Handle<detail::HCommandAllocator>;
    using CommandListHandle = Handle<detail::HCommandList>;
    using HeapHandle = Handle<detail::HHeap>;
    using QueryPoolHandle = Handle<detail::HQueryPool>;

    template<class TObject>
    class ObjectPtr {
    public:
        // Destroy: Device*, TObject* -> void
        using DestroyFn = void(*)(Device*, TObject*) noexcept;
        // Optional naming hook: TObject*, const char* -> void  (usually calls o->vt->setName)
        using NameFn = void(*)(TObject*, const char*) noexcept;

        ObjectPtr() = default;

        ObjectPtr(Device* d, TObject obj, DestroyFn dfn) noexcept
            : dev_(d), obj_(obj), destroy_(dfn){
        }

        ~ObjectPtr() { Reset(); }

        ObjectPtr(const ObjectPtr&) = delete;
        ObjectPtr& operator=(const ObjectPtr&) = delete;

        ObjectPtr(ObjectPtr&& o) noexcept { Swap(o); }
        ObjectPtr& operator=(ObjectPtr&& o) noexcept {
            if (this != &o) { Reset(); Swap(o); }
            return *this;
        }

        // Access
        TObject* operator->()       noexcept { return &obj_; }
        const TObject* operator->() const noexcept { return &obj_; }
        TObject& operator*()        noexcept { return obj_; }
        const TObject& operator*()  const noexcept { return obj_; }

        // State
        explicit operator bool() const noexcept { return dev_ && static_cast<bool>(obj_); }
        bool Valid() const noexcept { return !!*this; }
        Device* DevicePtr() const noexcept { return dev_; }
        TObject& Get() const noexcept { return obj_; } // by-ref view

        // Release ownership to caller (returns the raw object)
        TObject Release() noexcept {
            TObject t = obj_;
            dev_ = nullptr; destroy_ = nullptr;
            //obj_ = {}; // invalidate our copy
            return t;
        }

        // Destroy if owned
        void Reset() noexcept {
            if (dev_ && static_cast<bool>(obj_) && destroy_) {
                destroy_(dev_, &obj_);
            }
            dev_ = nullptr; destroy_ = nullptr;
            //obj_ = {};
        }

    private:
        void Swap(ObjectPtr& o) noexcept {
            std::swap(dev_, o.dev_);
            std::swap(obj_, o.obj_);
            std::swap(destroy_, o.destroy_);
        }

        Device* dev_{};        // non-owning
        TObject  obj_{};       // owning copy of the small POD wrapper
        DestroyFn destroy_{};  // how to destroy it
    };

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
        R10G10B10A2_Typeless, R10G10B10A2_UNorm, R10G10B10A2_UInt,
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

    enum PipelineLayoutFlags : uint32_t { PF_None = 0, PF_AllowInputAssembler = 1 << 0 }; // only for graphics pipelines

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

    enum class UavDim : uint32_t {
        Undefined,
        Buffer,
        Texture1D,
		Texture1DArray,
		Texture2D,
		Texture2DArray,
		Texture2DMS,
		Texture2DMSArray,
		Texture3D
	};

    enum class RtvDim : uint32_t {
        Undefined,
        Buffer,
        Texture1D,
        Texture1DArray,
        Texture2D,
        Texture2DArray,
        Texture2DMS,
        Texture2DMSArray,
        Texture3D
    };

    enum class DsvDim : uint32_t {
        Undefined,
        Texture1D,
        Texture1DArray,
        Texture2D,
        Texture2DArray,
        Texture2DMS,
        Texture2DMSArray
    };

    enum class BufferViewKind : uint32_t { Raw, Structured, Typed };

    enum class SrvDim : uint32_t {
        Undefined,
        Buffer,
        Texture1D, Texture1DArray,
        Texture2D, Texture2DArray,
        Texture2DMS, Texture2DMSArray,
        Texture3D,
        TextureCube, TextureCubeArray,
        AccelerationStruct, // DXR TLAS/BLAS SRV (VK_KHR_acceleration_structure)
    };

    // 0 => use API default (DX12_DEFAULT_SHADER_4_COMPONENT_MAPPING / RGBA identity in VK)
    using ComponentMapping = uint32_t;

    struct SrvDesc {
        SrvDim        dimension{ SrvDim::Undefined };
        ResourceHandle resource{};
        Format        formatOverride{ Format::Unknown }; // textures + typed buffers
        ComponentMapping componentMapping{ 0 };          // optional, 0 = default

        union {
            struct { // ===== Buffer SRV =====
                BufferViewKind kind{ BufferViewKind::Raw }; // Raw/Structured/Typed
                // RAW: firstElement in 32-bit units; TYPED/STRUCTURED: in elements
                uint64_t firstElement{ 0 };
                uint32_t numElements{ 0 };
                uint32_t structureByteStride{ 0 }; // Structured only
                // typed format comes from formatOverride
            } buffer;

            struct { // ===== TEX1D =====
                uint32_t mostDetailedMip{ 0 };
                uint32_t mipLevels{ 0 };
                float    minLodClamp{ 0.0f };
            } tex1D;

            struct { // ===== TEX1D ARRAY =====
                uint32_t mostDetailedMip{ 0 };
                uint32_t mipLevels{ 0 };
                uint32_t firstArraySlice{ 0 };
                uint32_t arraySize{ 0 };
                float    minLodClamp{ 0.0f };
            } tex1DArray;

            struct { // ===== TEX2D =====
                uint32_t mostDetailedMip{ 0 };
                uint32_t mipLevels{ 0 };
                uint32_t planeSlice{ 0 };     // for planar formats
                float    minLodClamp{ 0.0f };
            } tex2D;

            struct { // ===== TEX2D ARRAY =====
                uint32_t mostDetailedMip{ 0 };
                uint32_t mipLevels{ 0 };
                uint32_t firstArraySlice{ 0 };
                uint32_t arraySize{ 0 };
                uint32_t planeSlice{ 0 };
                float    minLodClamp{ 0.0f };
            } tex2DArray;

            struct { // ===== TEX2DMS =====
                // no fields
            } tex2DMS;

            struct { // ===== TEX2DMS ARRAY =====
                uint32_t firstArraySlice{ 0 };
                uint32_t arraySize{ 0 };
            } tex2DMSArray;

            struct { // ===== TEX3D =====
                uint32_t mostDetailedMip{ 0 };
                uint32_t mipLevels{ 0 };
                float    minLodClamp{ 0.0f };
            } tex3D;

            struct { // ===== CUBE =====
                uint32_t mostDetailedMip{ 0 };
                uint32_t mipLevels{ 0 };
                float    minLodClamp{ 0.0f };
            } cube;

            struct { // ===== CUBE ARRAY =====
                uint32_t mostDetailedMip{ 0 };
                uint32_t mipLevels{ 0 };
                uint32_t first2DArrayFace{ 0 };
                uint32_t numCubes{ 0 }; // arraySize / 6
                float    minLodClamp{ 0.0f };
            } cubeArray;

            struct { // ===== ACCEL STRUCT =====
                // no fields; resource is the AS buffer
            } accel;
        };
    };

    struct UavDesc {
        ResourceHandle  resource{};
		Format          formatOverride{ Format::Unknown }; // textures + typed buffers
        // Texture path
        UavDim        dimension{ UavDim::Buffer };
        union {
            struct { // ===== Buffer UAV =====
                BufferViewKind kind{ BufferViewKind::Raw }; // Raw/Structured/Typed
                // RAW: firstElement in 32-bit units; TYPED/STRUCTURED: in elements
                uint64_t firstElement{ 0 };
                uint32_t numElements{ 0 };
                uint32_t structureByteStride{ 0 }; // Structured only
				uint64_t counterOffsetInBytes{ 0 }; // optional, for append/consume buffers
			} buffer;
            struct { // ===== TEX1D =====
                uint32_t mipSlice{ 0 };
            } texture1D;
            struct { // ===== TEX1D ARRAY =====
                uint32_t mipSlice{ 0 };
                uint32_t firstArraySlice{ 0 };
                uint32_t arraySize{ 0 };
            } texture1DArray;
            struct { // ===== TEX2D =====
                uint32_t mipSlice{ 0 };
                uint32_t planeSlice{ 0 }; // for planar formats
            } texture2D;
            struct { // ===== TEX2D ARRAY =====
                uint32_t mipSlice{ 0 };
                uint32_t firstArraySlice{ 0 };
                uint32_t arraySize{ 0 };
                uint32_t planeSlice{ 0 }; // for planar formats
            } texture2DArray;
            struct { // ===== TEX2DMS =====
                // no fields
            } texture2DMS;
            struct { // ===== TEX2DMS ARRAY =====
                uint32_t firstArraySlice{ 0 };
                uint32_t arraySize{ 0 };
            } texture2DMSArray;
            struct { // ===== TEX3D =====
                uint32_t mipSlice{ 0 };
                uint32_t firstWSlice{ 0 };
                uint32_t wSize{ 0 };
			} texture3D;
        };
    };

    struct CbvDesc { uint64_t byteOffset = 0; uint32_t byteSize = 0; /* 256B aligned */ };

    // RTV/DSV descriptions (texture-only)
    struct RtvDesc {
        ResourceHandle texture{};
        RtvDim dimension{ RtvDim::Texture2D };
        TextureSubresourceRange range{};
        Format formatOverride{ Format::Unknown };
    };
    struct DsvDesc {
        ResourceHandle texture{};
        DsvDim dimension{ DsvDim::Texture2D };
        TextureSubresourceRange range{};
        Format formatOverride{ Format::Unknown };
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
	enum class ClearValueType : uint32_t { Color, DepthStencil };
	struct DepthStencilClearValue { float depth = 1.0f; uint8_t stencil = 0; };
    struct ClearValue {
		ClearValueType type = ClearValueType::Color;
		Format format = Format::Unknown;
		// union
        union {
            float rgba[4]{ 0,0,0,1 };
            DepthStencilClearValue depthStencil;
        };
    };
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
        RF_None = 0,
        RF_AllowRenderTarget = 1 << 0,
        RF_AllowDepthStencil = 1 << 1,
        RF_AllowUnorderedAccess = 1 << 2,
        RF_DenyShaderResource = 1 << 3,
        RF_AllowCrossAdapter = 1 << 4,
        RF_AllowSimultaneousAccess = 1 << 5,
        RF_VideoDecodeReferenceOnly = 1 << 6,
        RF_VideoEncodeReferenceOnly = 1 << 7,
        RF_RaytracingAccelerationStructure = 1 << 8,
    };
	// Define |= for ResourceFlags
	inline ResourceFlags operator|(ResourceFlags a, ResourceFlags b) {
		return static_cast<ResourceFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	inline ResourceFlags& operator|=(ResourceFlags& a, ResourceFlags b) {
		a = a | b;
		return a;
	}
    struct BufferDesc {
        uint64_t     sizeBytes = 0;
    };
    struct TextureDesc {
        Format       format = Format::Unknown;
        uint32_t     width = 1;
        uint32_t     height = 1;
        uint16_t     depthOrLayers = 1;     // depth for 3D, arraySize otherwise
        uint16_t     mipLevels = 1;
        uint32_t     sampleCount = 1;
        ResourceLayout initialLayout = ResourceLayout::Undefined;
        const ClearValue* optimizedClear = nullptr;    // optional, if RTV/DSV
    };
	enum class ResourceType : uint32_t { Unknown, Buffer, Texture1D, Texture2D, Texture3D};
    struct ResourceDesc { 
        ResourceType type = ResourceType::Unknown; 
        Memory memory = Memory::DeviceLocal;
        ResourceFlags flags;
        const char* debugName = nullptr;
        union { 
            TextureDesc texture; 
            BufferDesc buffer; 
        }; 
    };

    enum class HeapFlags : uint32_t {
        None = 0,
        // Mirror the most useful D3D12 flags
        AllowOnlyBuffers = 1u << 0,  // D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS
        AllowOnlyNonRtDsTextures = 1u << 1,  // D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES
        AllowOnlyRtDsTextures = 1u << 2,  // D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES
        DenyBuffers = 1u << 3,  // D3D12_HEAP_FLAG_DENY_BUFFERS
        DenyRtDsTextures = 1u << 4,  // D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES
        DenyNonRtDsTextures = 1u << 5,  // D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES
        Shared = 1u << 6,  // D3D12_HEAP_FLAG_SHARED
        SharedCrossAdapter = 1u << 7,  // D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER
        CreateNotResident = 1u << 8,  // D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT
        CreateNotZeroed = 1u << 9,  // D3D12_HEAP_FLAG_CREATE_NOT_ZEROED
        AllowAllBuffersAndTextures = 1u << 10  // D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES
    };

    inline HeapFlags operator|(HeapFlags a, HeapFlags b) {
        return static_cast<HeapFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline HeapFlags& operator|=(HeapFlags& a, HeapFlags b) { a = a | b; return a; }

    struct HeapDesc {
        uint64_t   sizeBytes = 0;      // total heap size
        uint64_t   alignment = 0;      // 0 -> choose default; otherwise 64KB or 4MB (MSAA) on DX12
        Memory     memory = Memory::DeviceLocal; // maps to HEAP_PROPERTIES.Type
        HeapFlags  flags = HeapFlags::None;
        const char* debugName{ nullptr };
    };

    struct VertexBufferView { ResourceHandle buffer{}; uint64_t offset = 0; uint32_t sizeBytes = 0; uint32_t stride = 0; };

    enum class Stage : uint32_t { Top, Draw, Pixel, Compute, Copy, Bottom }; 

    // ---------------- Submission & timelines (thin) ----------------
    struct TimelinePoint { TimelineHandle t; uint64_t value = 0; };
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
		bool discard{ false }; // if true, contents before the barrier are undefined (can skip some sync on certain APIs)
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

    struct UavClearUint { uint32_t v[4]; };
    struct UavClearFloat { float v[4]; };

    struct TextureCopyRegion {
        ResourceHandle texture{};
        uint32_t mip = 0;
        uint32_t arraySlice = 0;     // face for cubemaps (0..5) or array element
        uint32_t x = 0, y = 0, z = 0;
        uint32_t width = 0, height = 0, depth = 1; // depth slices for 3D
    };

    struct BufferTextureCopy {
        ResourceHandle buffer{};
        uint64_t offset = 0;         // byte offset in buffer
        uint32_t rowPitch = 0;       // bytes (must satisfy backend's row alignment)
        uint32_t slicePitch = 0;     // bytes
    };

    // What kind of queries we support at the RHI level
    enum class QueryType : uint32_t {
        Timestamp,            // DX12: TIMESTAMP heap | Vulkan: VK_QUERY_TYPE_TIMESTAMP
        PipelineStatistics,   // DX12: PIPELINE_STATISTICS / *_STATISTICS1 | Vulkan: VK_QUERY_TYPE_PIPELINE_STATISTICS
        Occlusion             // DX12: OCCLUSION | Vulkan: VK_QUERY_TYPE_OCCLUSION
    };

    // Cross-API pipeline stats bitmask (request only what you need).
    // Backends will mask out unsupported bits; check capabilities at device create.
    enum PipelineStatBits : uint64_t {
        PS_IAVertices = 1ull << 0,
        PS_IAPrimitives = 1ull << 1,
        PS_VSInvocations = 1ull << 2,
        PS_GSInvocations = 1ull << 3,
        PS_GSPrimitives = 1ull << 4,
        PS_CInvocations = 1ull << 5, // TessControl
        PS_CPrimitives = 1ull << 6, // TessControl output
        PS_EInvocations = 1ull << 7, // TessEval
        PS_PSInvocations = 1ull << 8,
        PS_CSInvocations = 1ull << 9,
        // Mesh/Task (DX12 PIPELINE_STATISTICS1; Vulkan requires VK_EXT_mesh_shader)
        PS_TaskInvocations = 1ull << 16,
        PS_MeshInvocations = 1ull << 17,
        PS_MeshPrimitives = 1ull << 18, // DX12 has MSPrimitives; Vulkan may not expose primitive count.
        PS_All = 0xFFFFFFFFFFFFFFFFull
    };
    enum class PipelineStatTypes : uint32_t {
        IAVertices,
        IAPrimitives,
        VSInvocations,
        GSInvocations,
        GSPrimitives,
        TSControlInvocations,   // a.k.a. HS
        TSEvaluationInvocations,// a.k.a. DS
        PSInvocations,
        CSInvocations,
        // Mesh/Task (DX12 *_STATISTICS1, Vulkan needs mesh shader extension)
        TaskInvocations,
        MeshInvocations,
        MeshPrimitives,
    };

    using PipelineStatsMask = uint64_t;

    // Query pool creation
    struct QueryPoolDesc {
        QueryType          type{};
        uint32_t           count = 0;         // total slots in the pool
        PipelineStatsMask  statsMask = 0;     // only for PipelineStatistics
        bool               requireAllStats = false; // if true and backend can’t support all bits -> Unsupported
    };

    struct QueryResultInfo {
        QueryType type{};
        uint32_t  count = 0;            // slots in the pool
        uint32_t  elementSize = 0;      // bytes per query result (native layout in the resolve buffer)
        uint32_t  elementAlignment = 8; // conservative; useful if you choose to pad
        // For timestamps/occlusion this is enough.
    };

    struct PipelineStatsFieldDesc {
        PipelineStatTypes field;
        uint32_t          byteOffset;   // offset within one element
        uint32_t          byteSize;     // usually 8 (u64 counters)
        bool              supported;    // false if backend can’t provide it
    };

    struct PipelineStatsLayout {
        QueryResultInfo   info;
        // Dense list; only supported fields included (or include all with supported=false)
        Span<PipelineStatsFieldDesc> fields;
    };


    // Convenience conversion for timestamps (RHI exposes a uniform "ticks per second")
    struct TimestampCalibration {
        uint64_t ticksPerSecond = 0; // DX12: queue->GetTimestampFrequency(); Vulkan: round(1e9 / timestampPeriod)
    };

    // ---------------- POD wrappers + VTables ----------------
    class Device;       class Queue;       class CommandList;        class Swapchain;       class CommandAllocator;
    struct DeviceVTable; struct QueueVTable; struct CommandListVTable; struct SwapchainVTable; struct CommandAllocatorVTable;
	class Resource;      struct ResourceVTable;
	class QueryPool;    struct QueryPoolVTable;
    class Pipeline;     struct PipelineVTable;
    class PipelineLayout; struct PipelineLayoutVTable;
    class CommandSignature; struct CommandSignatureVTable;
	class DescriptorHeap; struct DescriptorHeapVTable;
    class Sampler;     struct SamplerVTable;
    class Timeline;    struct TimelineVTable;
    class Heap;        struct HeapVTable;

    using CommandAllocatorPtr = ObjectPtr<CommandAllocator>;
    using CommandListPtr = ObjectPtr<CommandList>;
    using SwapchainPtr = ObjectPtr<Swapchain>;
    using DevicePtr = ObjectPtr<Device>;
    using ResourcePtr = ObjectPtr<Resource>;
    using QueryPoolPtr = ObjectPtr<QueryPool>;
    using PipelinePtr = ObjectPtr<Pipeline>;
    using PipelineLayoutPtr = ObjectPtr<PipelineLayout>;
    using CommandSignaturePtr = ObjectPtr<CommandSignature>;
    using DescriptorHeapPtr = ObjectPtr<DescriptorHeap>;
    using SamplerPtr = ObjectPtr<Sampler>;
    using TimelinePtr = ObjectPtr<Timeline>;
    using HeapPtr = ObjectPtr<Heap>;

    struct QueryPoolVTable {
        QueryResultInfo(*getQueryResultInfo)(QueryPool*) noexcept;
        PipelineStatsLayout(*getPipelineStatsLayout)(QueryPool*, PipelineStatsFieldDesc* outBuf, uint32_t outCap) noexcept;
		void (*setName)(QueryPool*, const char*) noexcept;
        uint32_t abi_version = 1;
	};
	class QueryPool {
	public:
        explicit QueryPool(QueryPoolHandle h = {}) : handle(h) {}
		void* impl{};
		const QueryPoolVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_QUERYPOOL_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
        QueryPoolHandle GetHandle() const noexcept { return handle; }
		QueryResultInfo GetQueryResultInfo() noexcept { return vt->getQueryResultInfo(this); }
		PipelineStatsLayout GetPipelineStatsLayout(PipelineStatsFieldDesc* outBuf, uint32_t outCap) noexcept {
			return vt->getPipelineStatsLayout(this, outBuf, outCap);
		}
        void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		QueryPoolHandle handle;
	};

    struct PipelineVTable {
		void (*setName)(Pipeline*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
	class Pipeline {
	public:
        explicit Pipeline(PipelineHandle h = {}) : handle(h) {}
		void* impl{};
		const PipelineVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_PIPELINE_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		PipelineHandle GetHandle() const noexcept { return handle; }
		void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		PipelineHandle handle;
	};

	struct PipelineLayoutVTable {
		void (*setName)(PipelineLayout*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
    class PipelineLayout {
	public:
        explicit PipelineLayout(PipelineLayoutHandle h = {}) : handle(h) {}
		void* impl{};
		const PipelineLayoutVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_PIPELINELAYOUT_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		PipelineLayoutHandle GetHandle() const noexcept { return handle; }
        void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		PipelineLayoutHandle handle;
	};

	struct CommandSignatureVTable {
		void (*setName)(CommandSignature*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
    class CommandSignature {
	public:
        explicit CommandSignature(CommandSignatureHandle h = {}) : handle(h) {}
		void* impl{};
		const CommandSignatureVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_COMMANDSIGNATURE_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		CommandSignatureHandle GetHandle() const noexcept { return handle; }
        void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		CommandSignatureHandle handle;
	};

    struct DescriptorHeapVTable {
		void (*setName)(DescriptorHeap*, const char*) noexcept;
        uint32_t abi_version = 1;
	};

    class DescriptorHeap {
	public:
		explicit DescriptorHeap(DescriptorHeapHandle h = {}) : handle(h) {}
        void* impl{};
		const DescriptorHeapVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_DESCRIPTORHEAP_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		DescriptorHeapHandle GetHandle() const noexcept { return handle; }
        void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		DescriptorHeapHandle handle;
	};

    struct SamplerVTable {
		void (*setName)(Sampler*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
    class Sampler {
	public:
        explicit Sampler(SamplerHandle h = {}) : handle(h) {}
		void* impl{};
		const SamplerVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_SAMPLER_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		SamplerHandle GetHandle() const noexcept { return handle; }
        void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		SamplerHandle handle;
	};

    struct TimelineVTable {
        uint64_t(*getCompletedValue)(Timeline*) noexcept;
        Result(*hostWait)(Timeline*, const TimelinePoint&) noexcept; // blocks until reached
		void (*setName)(Timeline*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
    class Timeline {
	public:
        explicit Timeline(TimelineHandle h = {}) : handle(h) {}
		void* impl{};
		const TimelineVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_TIMELINE_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		TimelineHandle GetHandle() const noexcept { return handle; }
		uint64_t GetCompletedValue() noexcept { return vt->getCompletedValue(this); }
		Result HostWait(const TimelinePoint& p) noexcept { return vt->hostWait(this, p); }
        void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		TimelineHandle handle;
	};

	struct HeapVTable {
		void (*setName)(Heap*, const char*) noexcept;
		uint32_t abi_version = 1;
	};
    class Heap {
	public:
        explicit Heap(HeapHandle h = {}) : handle(h) {}
		void* impl{};
		const HeapVTable* vt{};
		explicit constexpr operator bool() const noexcept {
			return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_HEAP_ABI_MIN;
		}
		constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
		HeapHandle GetHandle() const noexcept { return handle; }
        void SetName(const char* n) noexcept { vt->setName(this, n); }
	private:
		HeapHandle handle;
	};

    struct QueueVTable {
        Result(*submit)(Queue*, Span<CommandList>, const SubmitDesc&) noexcept;
        Result(*signal)(Queue*, const TimelinePoint&) noexcept;
        Result(*wait)(Queue*, const TimelinePoint&) noexcept;
        void (*setName)(Queue*, const char*) noexcept;
		uint32_t abi_version = 1;
    };

    class Queue {
    public:
        void* impl{}; 
        const QueueVTable* vt{}; 
        explicit constexpr operator bool() const noexcept {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_QUEUE_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
        Result Submit(Span<CommandList> lists, const SubmitDesc& s = {}) noexcept;
        Result Signal(const TimelinePoint& p) noexcept;
        Result Wait(const TimelinePoint& p) noexcept;
    };

    struct ResourceVTable {
        void (*map)(Resource*, void** data, uint64_t offset, uint64_t size) noexcept;
        void (*unmap)(Resource*, uint64_t writeOffset, uint64_t writeSize) noexcept;
        void (*setName)(Resource*, const char*) noexcept;
        uint32_t abi_version = 1;
    };
    class Resource {
    public:
        explicit Resource(ResourceHandle h = {}, bool isTexture = false) : handle(h) {}
        void* impl{}; // backend wrap (owns Handle32)
        const ResourceVTable* vt{}; // vtable
        explicit constexpr operator bool() const noexcept {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_RESOURCE_ABI_MIN;
        }
        bool IsTexture() const noexcept { return isTexture; }
        ResourceHandle GetHandle() const noexcept { return handle; }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; } // Naming
        inline void Map(void** data, uint64_t offset = 0, uint64_t size = ~0ull) noexcept { vt->map(this, data, offset, size); }
        inline void Unmap(uint64_t writeOffset, uint64_t writeSize) noexcept { vt->unmap(this, writeOffset, writeSize); }
        inline void SetName(const char* n) noexcept {
            vt->setName(this, n);
        }
    private:
        ResourceHandle handle;
        bool isTexture = false;
    };

    struct UavClearInfo {
        DescriptorSlot shaderVisible;   // SRV/UAV heap, shader-visible; REQUIRED on DX12
        DescriptorSlot cpuVisible;      // SRV/UAV heap, non shader-visible; REQUIRED on DX12
        Resource resource;        // the resource whose UAV is being cleared. Stores Resource instead of a handle because the backend may need to know if it's a buffer or texture. TODO: better solution?
    };


    struct CopyableFootprint {
        uint64_t offset;      // byte offset in the staging buffer
        uint32_t rowPitch;    // bytes per row (D3D12 is 256B aligned)
        uint32_t width;       // texels in X for the subresource
        uint32_t height;      // texels in Y for the subresource
        uint32_t depth;       // slices for 3D, else 1
    };

    struct FootprintRangeDesc {
        ResourceHandle texture{};
        uint32_t firstMip{ 0 };
        uint32_t mipCount{ 1 };
        uint32_t firstArraySlice{ 0 };
        uint32_t arraySize{ 1 };
        uint32_t firstPlane{ 0 };  // 0 for non-planar
        uint32_t planeCount{ 1 };  // 1 for non-planar
        uint64_t baseOffset{ 0 };  // starting byte offset into the (readback/upload) buffer
    };

    struct CopyableFootprintsInfo {
        uint32_t count{ 0 };     // number of footprints written
        uint64_t totalBytes{ 0 }; // total size from baseOffset that needs to be allocated
    };

    struct BufferTextureCopyFootprint {
        ResourceHandle texture;
        ResourceHandle buffer;

        uint32_t mip;
        uint32_t arraySlice;
        uint32_t x = 0, y = 0, z = 0;   // usually 0 for full-subresource copies

        // Placed footprint coming from Device::GetCopyableFootprints:
		CopyableFootprint footprint;
    };


    struct CommandListVTable {
        void (*end)(CommandList*) noexcept;
        void (*recycle)(CommandList*, const CommandAllocator& alloc) noexcept;
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
        void (*clearRenderTargetView)(CommandList*, ViewHandle, const ClearValue&) noexcept;
        void (*executeIndirect)(CommandList*,
            CommandSignatureHandle sig,
            ResourceHandle argumentBuffer, uint64_t argumentOffset,
            ResourceHandle countBuffer, uint64_t countOffset,
            uint32_t   maxCommandCount) noexcept;
        void (*setDescriptorHeaps)(CommandList*, DescriptorHeapHandle cbvSrvUav, DescriptorHeapHandle sampler) noexcept;
        void (*clearUavUint)(CommandList*, const UavClearInfo&, const UavClearUint&) noexcept;
        void (*clearUavFloat)(CommandList*, const UavClearInfo&, const UavClearFloat&) noexcept;
        void (*copyTextureToBuffer)(CommandList*, const BufferTextureCopyFootprint&) noexcept;
        void (*copyBufferToTexture)(CommandList*, const BufferTextureCopyFootprint&) noexcept;
        void (*copyTextureRegion)(CommandList*, const TextureCopyRegion&, const TextureCopyRegion&) noexcept;
        void (*copyBufferRegion)(CommandList*, ResourceHandle dst, uint64_t dstOffset, ResourceHandle src, uint64_t srcOffset, uint64_t numBytes) noexcept;
        void (*writeTimestamp)(CommandList*, QueryPoolHandle, uint32_t index, Stage stageHint) noexcept; // Timestamp: writes at 'index' (stage ignored on DX12, used on Vulkan)
        void (*beginQuery)(CommandList*, QueryPoolHandle, uint32_t index) noexcept; // Begin/End for occlusion & pipeline stats (no-op for timestamps)
        void (*endQuery)  (CommandList*, QueryPoolHandle, uint32_t index) noexcept;
        void (*resolveQueryData)(CommandList*, QueryPoolHandle, uint32_t firstQuery, uint32_t queryCount,
            ResourceHandle dstBuffer, uint64_t dstOffsetBytes) noexcept; // Resolve to a buffer; always 64-bit results (matches both APIs)
        void (*resetQueries)(CommandList*, QueryPoolHandle, uint32_t firstQuery, uint32_t queryCount) noexcept; // Vulkan requires resets before reuse; DX12 can no-op this.
        void (*setName)(CommandList*, const char*) noexcept;
        uint32_t abi_version = 1;
    };

    class CommandList { 
	public:
        explicit CommandList(CommandListHandle h = {}) : handle(h) {}
        void* impl{}; 
        const CommandListVTable* vt{}; 
        explicit constexpr operator bool() const noexcept {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_CL_ABI_MIN;
        }
		CommandListHandle GetHandle() const noexcept { return handle; }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
        void End() noexcept;
		void Recycle(const CommandAllocator& ca) noexcept;
        void BeginPass(const PassBeginInfo& p) noexcept;
        void EndPass() noexcept;
        void Barriers(const BarrierBatch& b) noexcept;
        void BindLayout(PipelineLayoutHandle l) noexcept;
        void BindPipeline(PipelineHandle p) noexcept;
        void SetVertexBuffers(uint32_t startSlot, uint32_t numViews, VertexBufferView* pBufferViews) noexcept;
        void SetIndexBuffer(ResourceHandle b, uint64_t offset, uint32_t sizeBytes, bool idx32) noexcept;
        void Draw(uint32_t v, uint32_t i, uint32_t fv, uint32_t fi) noexcept;
        void DrawIndexed(uint32_t i, uint32_t inst, uint32_t firstIdx, int32_t vOff, uint32_t firstI) noexcept;
        void Dispatch(uint32_t x, uint32_t y, uint32_t z) noexcept;
        void ClearRenderTargetView(ViewHandle v, const ClearValue& c) noexcept;
        void ExecuteIndirect(CommandSignatureHandle sig,
            ResourceHandle argBuf, uint64_t argOff,
            ResourceHandle cntBuf, uint64_t cntOff,
            uint32_t maxCount) noexcept;
		void SetDescriptorHeaps(DescriptorHeapHandle cbvSrvUav, DescriptorHeapHandle samp) noexcept;
		void ClearUavUint(const UavClearInfo& u, const UavClearUint& v) noexcept;
        void ClearUavFloat(const UavClearInfo& u, const UavClearFloat& v) noexcept;
		void CopyTextureToBuffer(const BufferTextureCopyFootprint& r) noexcept;
		void CopyBufferToTexture(const BufferTextureCopyFootprint& r) noexcept;
        void CopyTextureRegion(const TextureCopyRegion& dst, const TextureCopyRegion& src) noexcept;
        void CopyBufferRegion(ResourceHandle dst, uint64_t dstOffset, ResourceHandle src, uint64_t srcOffset, uint64_t numBytes) noexcept;
        void WriteTimestamp(QueryPoolHandle p, uint32_t idx, Stage s) noexcept;
        void BeginQuery(QueryPoolHandle p, uint32_t idx) noexcept;
        void EndQuery(QueryPoolHandle p, uint32_t idx) noexcept;
        void ResolveQueryData(QueryPoolHandle p, uint32_t first, uint32_t count, ResourceHandle dst, uint64_t off) noexcept;
        void ResetQueries(QueryPoolHandle p, uint32_t first, uint32_t count) noexcept;
		void SetName(const char* n) noexcept;

    private:
        CommandListHandle handle;
    };

    struct SwapchainVTable {
        uint32_t(*imageCount)(Swapchain*) noexcept;
        uint32_t(*currentImageIndex)(Swapchain*) noexcept;
        ViewHandle(*rtv)(Swapchain*, uint32_t img) noexcept; // RTV per image
        ResourceHandle(*image)(Swapchain*, uint32_t img) noexcept; // texture handle per image
        Result(*present)(Swapchain*, bool vsync) noexcept; // Present
        Result(*resizeBuffers)(Swapchain*, uint32_t bufferCount, uint32_t w, uint32_t h, Format newFormat, uint32_t flags) noexcept;
        void (*setName)(Swapchain*, const char*) noexcept;
        uint32_t abi_version = 1;
    };

    class Swapchain {
    public:
        void* impl{}; 
        const SwapchainVTable* vt{}; 
        explicit constexpr operator bool() const noexcept {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_SC_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void reset() noexcept { impl = nullptr; vt = nullptr; }
        inline uint32_t ImageCount() noexcept;
        inline uint32_t CurrentImageIndex() noexcept;
        inline ViewHandle RTV(uint32_t i) noexcept;
        inline ResourceHandle Image(uint32_t i) noexcept;
        inline Result Present(bool vsync) noexcept;
        inline Result ResizeBuffers(uint32_t bufferCount, uint32_t w, uint32_t h, Format newFmt, uint32_t flags) noexcept {
            return vt->resizeBuffers ? vt->resizeBuffers(this, bufferCount, w, h, newFmt, flags) : Result::Unsupported;
        }
    };

    struct DeviceVTable {
        PipelinePtr(*createPipelineFromStream)(Device*, const PipelineStreamItem* items, uint32_t count) noexcept;
        PipelineLayoutPtr(*createPipelineLayout)(Device*, const PipelineLayoutDesc&) noexcept;
        CommandSignaturePtr(*createCommandSignature)(Device*, const CommandSignatureDesc&, PipelineLayoutHandle /*layoutOrNull*/) noexcept;
        CommandAllocatorPtr(*createCommandAllocator)(Device*, QueueKind) noexcept;
        CommandListPtr(*createCommandList)(Device*, QueueKind, CommandAllocator) noexcept;
        SwapchainPtr(*createSwapchain)(Device*, void* hwnd, uint32_t w, uint32_t h, Format fmt, uint32_t bufferCount, bool allowTearing) noexcept;
        DescriptorHeapPtr(*createDescriptorHeap)(Device*, const DescriptorHeapDesc&) noexcept;

        Result(*createConstantBufferView)(Device*, DescriptorSlot dst, ResourceHandle, const CbvDesc&) noexcept;
        Result(*createShaderResourceView)(Device*, DescriptorSlot dst, const SrvDesc&) noexcept;
        Result(*createUnorderedAccessView)(Device*, DescriptorSlot dst, const UavDesc&) noexcept;
        Result(*createRenderTargetView)(Device*, DescriptorSlot dst, const RtvDesc&) noexcept;
        Result(*createDepthStencilView)(Device*, DescriptorSlot dst, const DsvDesc&) noexcept;
        Result(*createSampler)(Device*, DescriptorSlot dst, const SamplerDesc&) noexcept;
        ResourcePtr(*createCommittedResource)(Device*, const ResourceDesc&) noexcept;
        TimelinePtr(*createTimeline)(Device*, uint64_t initialValue, const char* debugName) noexcept;
        HeapPtr(*createHeap)(Device*, const HeapDesc&) noexcept;
        ResourcePtr(*createPlacedResource)(Device*, HeapHandle, uint64_t offset, const ResourceDesc&) noexcept;
        QueryPoolPtr(*createQueryPool)(Device*, const QueryPoolDesc&) noexcept;

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
        void (*destroyTimeline)(Device*, TimelineHandle) noexcept;
        void (*destroyHeap)(Device*, HeapHandle) noexcept;
        void (*destroyQueryPool)(Device*, QueryPoolHandle) noexcept;

        Queue(*getQueue)(Device*, QueueKind) noexcept;
        Result(*deviceWaitIdle)(Device*) noexcept;
        void (*flushDeletionQueue)(Device*) noexcept;
        uint32_t(*getDescriptorHandleIncrementSize)(Device*, DescriptorHeapType) noexcept;
        TimestampCalibration(*getTimestampCalibration)(Device*, QueueKind) noexcept;
        CopyableFootprintsInfo(*getCopyableFootprints)(Device*, const FootprintRangeDesc&, CopyableFootprint* out, uint32_t outCap) noexcept;

		// Optional debug name setters (can be nullopt)
        void (*setNameBuffer)(Device*, ResourceHandle, const char*) noexcept;
        void (*setNameTexture)(Device*, ResourceHandle, const char*) noexcept;
        void (*setNameSampler)(Device*, SamplerHandle, const char*) noexcept;
        void (*setNamePipelineLayout)(Device*, PipelineLayoutHandle, const char*) noexcept;
        void (*setNamePipeline)(Device*, PipelineHandle, const char*) noexcept;
        void (*setNameCommandSignature)(Device*, CommandSignatureHandle, const char*) noexcept;
        void (*setNameDescriptorHeap)(Device*, DescriptorHeapHandle, const char*) noexcept;
        void (*setNameTimeline)(Device*, TimelineHandle, const char*) noexcept;
		void (*setNameHeap)(Device*, HeapHandle, const char*) noexcept;

        void (*destroyDevice)(Device*) noexcept;
        uint32_t abi_version = 1;
    };

    struct CommandAllocatorVTable {
        void (*reset)(CommandAllocator*) noexcept;  // allocator Reset()
        uint32_t abi_version = 1;
    };

    class CommandAllocator {
    public:
        explicit CommandAllocator(CommandAllocatorHandle handle = {}) : handle(handle) {}
        void* impl{}; // backend wrap (owns Handle32)
        const CommandAllocatorVTable* vt{}; // vtable
        explicit constexpr operator bool() const noexcept {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_CA_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
		constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; } // Naming conflict with vt->reset
        inline void Recycle() noexcept { vt->reset(this); } // GPU-side reset (allocator->Reset)
		CommandAllocatorHandle GetHandle() const noexcept { return handle; }

    private:
        CommandAllocatorHandle handle;
    };

    class Device {
	public:
        void* impl{}; 
        const DeviceVTable* vt{};
        explicit constexpr operator bool() const noexcept {
            return impl != nullptr && vt != nullptr && vt->abi_version >= RHI_DEVICE_ABI_MIN;
        }
        constexpr bool IsValid() const noexcept { return static_cast<bool>(*this); }
        constexpr void Reset() noexcept { impl = nullptr; vt = nullptr; }
        inline PipelinePtr CreatePipeline(const PipelineStreamItem* items, uint32_t count) noexcept { return vt->createPipelineFromStream(this, items, count); }
        inline CommandListPtr CreateCommandList(QueueKind q, CommandAllocator alloc) noexcept { return vt->createCommandList(this, q, alloc); }
        inline void DestroyCommandList(CommandList* cl) noexcept { vt->destroyCommandList(this, cl); }
        inline Queue GetQueue(QueueKind q) noexcept { return vt->getQueue(this, q); }
        inline Result WaitIdle() noexcept { return vt->deviceWaitIdle(this); }
        inline void FlushDeletionQueue() noexcept { vt->flushDeletionQueue(this); }
        inline SwapchainPtr CreateSwapchain(void* hwnd, uint32_t w, uint32_t h, Format fmt, uint32_t buffers, bool allowTearing) noexcept { return vt->createSwapchain(this, hwnd, w, h, fmt, buffers, allowTearing); }
        inline void DestroySwapchain(Swapchain* sc) noexcept { vt->destroySwapchain(this, sc); }
        inline PipelineLayoutPtr CreatePipelineLayout(const PipelineLayoutDesc& d) noexcept { return vt->createPipelineLayout(this, d); }
        inline void DestroyPipelineLayout(PipelineLayoutHandle h) noexcept { vt->destroyPipelineLayout(this, h); }
        inline CommandSignaturePtr CreateCommandSignature(const CommandSignatureDesc& d, PipelineLayoutHandle layout) noexcept { return vt->createCommandSignature(this, d, layout); }
        inline void DestroyCommandSignature(CommandSignatureHandle h) noexcept { vt->destroyCommandSignature(this, h); }
		inline DescriptorHeapPtr CreateDescriptorHeap(const DescriptorHeapDesc& d) noexcept { return vt->createDescriptorHeap(this, d); }
		inline void DestroyDescriptorHeap(DescriptorHeapHandle h) noexcept { vt->destroyDescriptorHeap(this, h); }
        inline Result CreateConstantBufferView(DescriptorSlot s, ResourceHandle b, const CbvDesc& d) noexcept { return vt->createConstantBufferView(this, s, b, d); }
        inline Result CreateShaderResourceView(DescriptorSlot s, const SrvDesc& d) noexcept { return vt->createShaderResourceView(this, s, d); }
        inline Result CreateUnorderedAccessView(DescriptorSlot s, const UavDesc& d) noexcept { return vt->createUnorderedAccessView(this, s, d); }
        inline Result CreateSampler(DescriptorSlot s, const SamplerDesc& d) noexcept { return vt->createSampler(this, s, d); }
        inline Result CreateRenderTargetView(DescriptorSlot s, const RtvDesc& d) noexcept { return vt->createRenderTargetView(this, s, d); }
        inline Result CreateDepthStencilView(DescriptorSlot s, const DsvDesc& d) noexcept { return vt->createDepthStencilView(this, s, d); }
        inline CommandAllocatorPtr CreateCommandAllocator(QueueKind q) noexcept { return vt->createCommandAllocator(this, q); }
		inline void DestroyCommandAllocator(CommandAllocator* a) noexcept { vt->destroyCommandAllocator(this, a); }
		inline ResourcePtr CreateCommittedResource(const ResourceDesc& d) noexcept { return vt->createCommittedResource(this, d); }
        inline void DestroySampler(SamplerHandle h) noexcept { vt->destroySampler(this, h); }
		inline void DestroyPipeline(PipelineHandle h) noexcept { vt->destroyPipeline(this, h); }
		inline void DestroyBuffer(ResourceHandle h) noexcept { vt->destroyBuffer(this, h); }
		inline void DestroyTexture(ResourceHandle h) noexcept { vt->destroyTexture(this, h); }
		inline uint32_t GetDescriptorHandleIncrementSize(DescriptorHeapType t) noexcept { return vt->getDescriptorHandleIncrementSize(this, t); }
        inline TimelinePtr CreateTimeline(uint64_t initial = 0, const char* name = nullptr) noexcept { return vt->createTimeline(this, initial, name); }
        inline void DestroyTimeline(TimelineHandle t) noexcept { vt->destroyTimeline(this, t); }
        inline HeapPtr CreateHeap(const HeapDesc& h) noexcept { return vt->createHeap(this, h); }
        inline void DestroyHeap(HeapHandle h) noexcept { vt->destroyHeap(this, h); }
        inline ResourcePtr CreatePlacedResource(HeapHandle heap, uint64_t offset, const ResourceDesc& rd) noexcept { return vt->createPlacedResource(this, heap, offset, rd); }
        inline QueryPoolPtr CreateQueryPool(const QueryPoolDesc& d) noexcept { return vt->createQueryPool(this, d); }
        inline void DestroyQueryPool(QueryPoolHandle h) noexcept { vt->destroyQueryPool(this, h); }
        inline TimestampCalibration GetTimestampCalibration(QueueKind q) noexcept { return vt->getTimestampCalibration(this, q); }
        inline CopyableFootprintsInfo GetCopyableFootprints(const FootprintRangeDesc& r, CopyableFootprint* out, uint32_t outCap) noexcept {
            return vt->getCopyableFootprints(this, r, out, outCap);
		}
        inline void Destroy() noexcept { vt->destroyDevice(this); impl = nullptr; vt = nullptr; }
    };


    inline Result Queue::Submit(Span<CommandList> lists, const SubmitDesc& s) noexcept { return vt->submit(this, lists, s); }
    inline Result Queue::Signal(const TimelinePoint& p) noexcept { return vt->signal(this, p); }
    inline Result Queue::Wait(const TimelinePoint& p) noexcept { return vt->wait(this, p); }

    inline uint32_t Swapchain::ImageCount() noexcept { return vt->imageCount(this); }
    inline uint32_t Swapchain::CurrentImageIndex() noexcept { return vt->currentImageIndex(this); }
    inline ViewHandle Swapchain::RTV(uint32_t i) noexcept { return vt->rtv(this, i); }
    inline ResourceHandle Swapchain::Image(uint32_t i) noexcept { return vt->image(this, i); }
    inline Result Swapchain::Present(bool vsync) noexcept { return vt->present(this, vsync); }

    inline void CommandList::End() noexcept { vt->end(this); }
    inline void CommandList::Recycle(const CommandAllocator& alloc) noexcept { vt->recycle(this, alloc); }
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
    inline void CommandList::ClearRenderTargetView(ViewHandle v, const ClearValue& c) noexcept { vt->clearRenderTargetView(this, v, c); }
    inline void CommandList::ExecuteIndirect(CommandSignatureHandle sig, ResourceHandle argBuf, uint64_t argOff, ResourceHandle cntBuf, uint64_t cntOff, uint32_t maxCount) noexcept {
        vt->executeIndirect(this, sig, argBuf, argOff, cntBuf, cntOff, maxCount);
    }
    inline void CommandList::SetDescriptorHeaps(DescriptorHeapHandle csu, DescriptorHeapHandle samp) noexcept { vt->setDescriptorHeaps(this, csu, samp); }
    inline void CommandList::ClearUavUint(const UavClearInfo& i, const UavClearUint& v) noexcept { vt->clearUavUint(this, i, v); }
    inline void CommandList::CopyTextureToBuffer(const BufferTextureCopyRegion& r) noexcept {
        vt->copyTextureToBuffer(this, r);
    }
    inline void CommandList::CopyBufferToTexture(const BufferTextureCopyRegion& r) noexcept {
        vt->copyBufferToTexture(this, r);
    }    inline void CommandList::ClearUavFloat(const UavClearInfo& i, const UavClearFloat& v) noexcept { vt->clearUavFloat(this, i, v); }
    inline void CommandList::CopyTextureRegion(const TextureCopyRegion& dst, const TextureCopyRegion& src) noexcept { vt->copyTextureRegion(this, dst, src); }
    inline void CommandList::CopyBufferRegion(ResourceHandle dst, uint64_t dstOffset, ResourceHandle src, uint64_t srcOffset, uint64_t numBytes) noexcept {
        vt->copyBufferRegion(this, dst, dstOffset, src, srcOffset, numBytes);
    }
    inline void CommandList::WriteTimestamp(QueryPoolHandle p, uint32_t idx, Stage s) noexcept { vt->writeTimestamp(this, p, idx, s); }
    inline void CommandList::BeginQuery(QueryPoolHandle p, uint32_t idx) noexcept { vt->beginQuery(this, p, idx); }
    inline void CommandList::EndQuery(QueryPoolHandle p, uint32_t idx) noexcept { vt->endQuery(this, p, idx); }
    inline void CommandList::ResolveQueryData(QueryPoolHandle p, uint32_t first, uint32_t count, ResourceHandle dst, uint64_t off) noexcept {
        vt->resolveQueryData(this, p, first, count, dst, off);
    }
    inline void CommandList::ResetQueries(QueryPoolHandle p, uint32_t first, uint32_t count) noexcept {
        vt->resetQueries(this, p, first, count);
    }
    inline void CommandList::SetName(const char* n) noexcept { vt->setName(this, n); }

    struct DeviceCreateInfo { Backend backend = Backend::D3D12; uint32_t framesInFlight = 3; bool enableDebug = true; };

    static inline ShaderBinary DXIL(ID3DBlob* blob) {
        return { blob ? blob->GetBufferPointer() : nullptr,
                 blob ? static_cast<uint32_t>(blob->GetBufferSize()) : 0u };
    }

    inline CommandAllocatorPtr MakeCommandAllocatorPtr(Device* d, CommandAllocator ca) noexcept {
        return CommandAllocatorPtr(
            d, ca,
            // Destroy
            [](Device* dev, CommandAllocator* p) noexcept { if (dev && p) dev->DestroyCommandAllocator(p); }
            // TODO: Name hook for allocator
        );
    }

    inline CommandListPtr MakeCommandListPtr(Device* d, CommandList cl) noexcept {
        return CommandListPtr(
            d, cl,
            [](Device* dev, CommandList* p) noexcept { if (dev && p) dev->DestroyCommandList(p); }
        );
    }

    inline SwapchainPtr MakeSwapchainPtr(Device* d, Swapchain sc) noexcept {
        return SwapchainPtr(
            d, sc,
            [](Device* dev, Swapchain* p) noexcept { if (dev && p) dev->DestroySwapchain(p); }
        );
    }

    inline DevicePtr MakeDevicePtr(Device d) noexcept {
        return DevicePtr(
            &d, d,
            // DestroyFn for Device ignores the TObject* and calls destroy on the object itself
            [](Device* /*ignored*/, Device* self) noexcept {
                if (self && self->IsValid()) self->Destroy();
            }
			// TODO: Name hook for Device
        );
    }
    inline ResourcePtr MakeTexturePtr(Device* d, Resource r) noexcept {
        return ResourcePtr(
            d, r,
            [](Device* dev, Resource* p) noexcept { if (dev && p) dev->DestroyTexture(p->GetHandle()); }
        );
	}
    inline ResourcePtr MakeBufferPtr(Device* d, Resource r) noexcept {
        return ResourcePtr(
            d, r,
            [](Device* dev, Resource* p) noexcept { if (dev && p) dev->DestroyBuffer(p->GetHandle()); }
        );
	}
    inline QueryPoolPtr MakeQueryPoolPtr(Device* d, QueryPool h) noexcept {
        return QueryPoolPtr(d, h,
            [](Device* dev, QueryPool* hh) noexcept { if (dev) dev->DestroyQueryPool(hh->GetHandle()); }
		);
	}
    inline PipelinePtr MakePipelinePtr(Device* d, Pipeline h) noexcept {
        return PipelinePtr(d, h,
            [](Device* dev, Pipeline* hh) noexcept { if (dev) dev->DestroyPipeline(hh->GetHandle()); }
        );
	}
    inline PipelineLayoutPtr MakePipelineLayoutPtr(Device* d, PipelineLayout h) noexcept {
        return PipelineLayoutPtr(d, h,
            [](Device* dev, PipelineLayout* hh) noexcept { if (dev) dev->DestroyPipelineLayout(hh->GetHandle()); }
		);
	}
    inline CommandSignaturePtr MakeCommandSignaturePtr(Device* d, CommandSignature h) noexcept {
        return CommandSignaturePtr(d, h,
            [](Device* dev, CommandSignature* hh) noexcept { if (dev) dev->DestroyCommandSignature(hh->GetHandle()); }
		);
	}
	inline DescriptorHeapPtr MakeDescriptorHeapPtr(Device* d, DescriptorHeap h) noexcept {
        return DescriptorHeapPtr(d, h,
			[](Device* dev, DescriptorHeap* hh) noexcept { if (dev) dev->DestroyDescriptorHeap(hh->GetHandle()); }
		);
	}
	inline SamplerPtr MakeSamplerPtr(Device* d, Sampler h) noexcept {
		return SamplerPtr(d, h,
			[](Device* dev, Sampler* hh) noexcept { if (dev) dev->DestroySampler(hh->GetHandle()); }
		);
	}
    inline TimelinePtr MakeTimelinePtr(Device* d, Timeline h) noexcept {
        return TimelinePtr(d, h,
            [](Device* dev, Timeline* hh) noexcept { if (dev) dev->DestroyTimeline(hh->GetHandle()); }
        );
	}
    inline HeapPtr MakeHeapPtr(Device* d, Heap h) noexcept {
        return HeapPtr(d, h,
            [](Device* dev, Heap* hh) noexcept { if (dev) dev->DestroyHeap(hh->GetHandle()); }
		);
	}

    inline void name_buffer(Device* d, ResourceHandle h, const char* n) noexcept {
        if (d && d->vt && d->vt->setNameBuffer) d->vt->setNameBuffer(d, h, n);
    }
    inline void name_texture(Device* d, ResourceHandle h, const char* n) noexcept {
        if (d && d->vt && d->vt->setNameTexture) d->vt->setNameTexture(d, h, n);
    }
    inline void name_pipeline(Device* d, PipelineHandle h, const char* n) noexcept {
        if (d && d->vt && d->vt->setNamePipeline) d->vt->setNamePipeline(d, h, n);
    }
    inline void name_layout(Device* d, PipelineLayoutHandle h, const char* n) noexcept {
        if (d && d->vt && d->vt->setNamePipelineLayout) d->vt->setNamePipelineLayout(d, h, n);
    }
    inline void name_cmdsig(Device* d, CommandSignatureHandle h, const char* n) noexcept {
        if (d && d->vt && d->vt->setNameCommandSignature) d->vt->setNameCommandSignature(d, h, n);
    }
    inline void name_heap(Device* d, DescriptorHeapHandle h, const char* n) noexcept {
        if (d && d->vt && d->vt->setNameDescriptorHeap) d->vt->setNameDescriptorHeap(d, h, n);
    }
    inline void name_sampler(Device* d, SamplerHandle h, const char* n) noexcept {
        if (d && d->vt && d->vt->setNameSampler) d->vt->setNameSampler(d, h, n);
    }
    inline void name_timeline(Device* d, TimelineHandle h, const char* n) noexcept {
        if (d && d->vt && d->vt->setNameTimeline) d->vt->setNameTimeline(d, h, n);
    }
    inline void name_heap(Device* d, HeapHandle h, const char* n) noexcept {
        if (d && d->vt && d->vt->setNameHeap) d->vt->setNameHeap(d, h, n);
    }

    DevicePtr CreateD3D12Device(const DeviceCreateInfo& ci) noexcept; // implemented in rhi_dx12.cpp

}