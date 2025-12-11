#pragma once
#include <cstdint>
#include "rhi.h"
#include "rhi_interop.h" // for QueryNative* + interop structs
#include "rhi_interop_dx12.h" // Used when building with D3D12
#include "rhi_colors.h"

// Optional feature switches (define them in your build if needed):
// - RHI_ENABLE_PIX            -> enable PIX markers on D3D12 (requires <pix3.h>)
// - RHI_ENABLE_VULKAN_MARKERS -> enable VK_EXT_debug_utils markers on Vulkan

#if __has_include(<pix3.h>)
#ifndef USE_PIX
#define USE_PIX 1
#endif
#include <pix3.h>
#ifndef RHI_ENABLE_PIX
#define RHI_ENABLE_PIX 1
#endif
#endif

#if defined(RHI_ENABLE_VULKAN_MARKERS)
#include <vulkan/vulkan.h>
#endif

namespace rhi::debug {

    // Colors are 0xAARRGGBB
    using Color = rhi::colors::RGBA8;

#if RHI_HAS_PIX
    inline std::uint64_t to_pix(Color c) noexcept {
        using namespace rhi::colors;
        return PIX_COLOR(r(c), g(c), b(c));
    }
#endif

    constexpr Color RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) noexcept {
        return (Color(a) << 24) | (Color(r) << 16) | (Color(g) << 8) | Color(b);
    }

    inline void toRGBAf(Color c, float out[4]) noexcept {
        const float inv = 1.0f / 255.0f;
        out[0] = ((c >> 16) & 0xFF) * inv; // R
        out[1] = ((c >> 8) & 0xFF) * inv; // G
        out[2] = ((c >> 0) & 0xFF) * inv; // B
        out[3] = ((c >> 24) & 0xFF) * inv; // A
    }

    // Optional one-time init for backends that need function pointers (Vulkan).
    // D3D12 + PIX requires no init; Vulkan will query vk* proc addrs here.
    bool Init(Device d) noexcept;
    void Shutdown(Device /*d*/) noexcept; // currently a no-op

    // Command list markers
    void Begin(CommandList cmd, Color color, const char* name) noexcept;
    void End(CommandList cmd) noexcept;
    void Marker(CommandList cmd, Color color, const char* name) noexcept;

    // Queue markers
    void Begin(Queue q, Color color, const char* name) noexcept;
    void End(Queue q) noexcept;
    void Marker(Queue q, Color color, const char* name) noexcept;

    // RAII scopes
    struct Scope {
        Scope(CommandList cmd, Color color, const char* name) noexcept : cmd_(cmd), active_(true) {
            Begin(cmd_, color, name);
        }
        ~Scope() { if (active_) End(cmd_); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&& o) noexcept : cmd_(o.cmd_), active_(o.active_) { o.active_ = false; }
        Scope& operator=(Scope&& o) noexcept {
            if (this != &o) { if (active_) End(cmd_); cmd_ = o.cmd_; active_ = o.active_; o.active_ = false; }
            return *this;
        }
    private:
        CommandList cmd_{};
        bool active_{ false };
    };

    struct QueueScope {
        QueueScope(Queue q, Color color, const char* name) noexcept : q_(q), active_(true) {
            Begin(q_, color, name);
        }
        ~QueueScope() { if (active_) End(q_); }
        QueueScope(const QueueScope&) = delete;
        QueueScope& operator=(const QueueScope&) = delete;
        QueueScope(QueueScope&& o) noexcept : q_(o.q_), active_(o.active_) { o.active_ = false; }
        QueueScope& operator=(QueueScope&& o) noexcept {
            if (this != &o) { if (active_) End(q_); q_ = o.q_; active_ = o.active_; o.active_ = false; }
            return *this;
        }
    private:
        Queue q_{};
        bool active_{ false };
    };

    // -------------------- Implementation --------------------

    namespace detail {

#if defined(RHI_ENABLE_VULKAN_MARKERS)
        // Cached proc addrs; populated by debug::Init(...)
        inline PFN_vkCmdBeginDebugUtilsLabelEXT  vkCmdBeginDebugUtilsLabelEXT_ = nullptr;
        inline PFN_vkCmdEndDebugUtilsLabelEXT    vkCmdEndDebugUtilsLabelEXT_ = nullptr;
        inline PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabelEXT_ = nullptr;
        inline bool vk_ready_ = false;
#endif

    } // namespace detail

    inline bool Init(Device d) noexcept {
        (void)d;
#if defined(RHI_ENABLE_VULKAN_MARKERS)
        // Try to fetch Vulkan proc addrs if we have a Vulkan device behind this RHI device.
        VulkanDeviceInfo vinfo{};
        if (QueryNativeDevice(d, RHI_IID_VK_DEVICE, &vinfo, sizeof(vinfo)) && vinfo.instance && vinfo.device) {
            auto inst = reinterpret_cast<VkInstance>(vinfo.instance);
            auto dev = reinterpret_cast<VkDevice>(vinfo.device);

            auto vkGetInstanceProcAddr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                vkGetInstanceProcAddr);
            auto vkGetDeviceProcAddr_ = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                vkGetDeviceProcAddr);

            if (vkGetInstanceProcAddr_ && vkGetDeviceProcAddr_) {
                detail::vkCmdBeginDebugUtilsLabelEXT_ =
                    reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
                        vkGetInstanceProcAddr_(inst, "vkCmdBeginDebugUtilsLabelEXT"));
                detail::vkCmdEndDebugUtilsLabelEXT_ =
                    reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
                        vkGetInstanceProcAddr_(inst, "vkCmdEndDebugUtilsLabelEXT"));
                detail::vkCmdInsertDebugUtilsLabelEXT_ =
                    reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
                        vkGetInstanceProcAddr_(inst, "vkCmdInsertDebugUtilsLabelEXT"));

                // Some loaders require device proc addrs; try those as fallback
                if (!detail::vkCmdBeginDebugUtilsLabelEXT_)
                    detail::vkCmdBeginDebugUtilsLabelEXT_ =
                    reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
                        vkGetDeviceProcAddr_(dev, "vkCmdBeginDebugUtilsLabelEXT"));
                if (!detail::vkCmdEndDebugUtilsLabelEXT_)
                    detail::vkCmdEndDebugUtilsLabelEXT_ =
                    reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
                        vkGetDeviceProcAddr_(dev, "vkCmdEndDebugUtilsLabelEXT"));
                if (!detail::vkCmdInsertDebugUtilsLabelEXT_)
                    detail::vkCmdInsertDebugUtilsLabelEXT_ =
                    reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
                        vkGetDeviceProcAddr_(dev, "vkCmdInsertDebugUtilsLabelEXT"));

                detail::vk_ready_ =
                    detail::vkCmdBeginDebugUtilsLabelEXT_ &&
                    detail::vkCmdEndDebugUtilsLabelEXT_ &&
                    detail::vkCmdInsertDebugUtilsLabelEXT_;
            }
        }
#endif
        return true;
    }

    inline void Shutdown(Device /*d*/) noexcept {
        // Nothing to do; debuggers hook globally and Vulkan function pointers can stay cached.
    }

    // ---------------- D3D12 (PIX) command list ----------------
    inline void Begin(CommandList cmd, Color color, const char* name) noexcept {
#if RHI_ENABLE_PIX
        if (auto* cl = rhi::dx12::get_cmd_list(cmd)) { PIXBeginEvent(cl, color, name ? name : ""); return; }
#endif
#if defined(RHI_ENABLE_VULKAN_MARKERS)
        if (detail::vk_ready_) {
            VulkanCmdBufInfo info{};
            if (QueryNativeCmdList(cmd, RHI_IID_VK_COMMAND_BUFFER, &info, sizeof(info)) && info.commandBuffer) {
                VkDebugUtilsLabelEXT label{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
                label.pLabelName = name ? name : "";
                float c[4]; toRGBAf(color, c); label.color[0] = c[0]; label.color[1] = c[1]; label.color[2] = c[2]; label.color[3] = c[3];
                detail::vkCmdBeginDebugUtilsLabelEXT_(reinterpret_cast<VkCommandBuffer>(info.commandBuffer), &label);
                return;
            }
        }
#endif
        (void)cmd; (void)color; (void)name; // no-op
    }

    inline void End(CommandList cmd) noexcept {
#if RHI_ENABLE_PIX
        if (auto* cl = rhi::dx12::get_cmd_list(cmd)) { PIXEndEvent(cl); return; }
#endif
#if defined(RHI_ENABLE_VULKAN_MARKERS)
        if (detail::vk_ready_) {
            VulkanCmdBufInfo info{};
            if (QueryNativeCmdList(cmd, RHI_IID_VK_COMMAND_BUFFER, &info, sizeof(info)) && info.commandBuffer) {
                detail::vkCmdEndDebugUtilsLabelEXT_(reinterpret_cast<VkCommandBuffer>(info.commandBuffer));
                return;
            }
        }
#endif
        (void)cmd; // no-op
    }

    inline void Marker(CommandList cmd, Color color, const char* name) noexcept {
#if RHI_ENABLE_PIX
        if (auto* cl = rhi::dx12::get_cmd_list(cmd)) { PIXSetMarker(cl, color, name ? name : ""); return; }
#endif
#if defined(RHI_ENABLE_VULKAN_MARKERS)
        if (detail::vk_ready_) {
            VulkanCmdBufInfo info{};
            if (QueryNativeCmdList(cmd, RHI_IID_VK_COMMAND_BUFFER, &info, sizeof(info)) && info.commandBuffer) {
                VkDebugUtilsLabelEXT label{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
                label.pLabelName = name ? name : "";
                float c[4]; toRGBAf(color, c); label.color[0] = c[0]; label.color[1] = c[1]; label.color[2] = c[2]; label.color[3] = c[3];
                detail::vkCmdInsertDebugUtilsLabelEXT_(reinterpret_cast<VkCommandBuffer>(info.commandBuffer), &label);
                return;
            }
        }
#endif
        (void)cmd; (void)color; (void)name; // no-op
    }

    // ---------------- D3D12 (PIX) queue ----------------
    inline void Begin(Queue q, Color color, const char* name) noexcept {
#if RHI_ENABLE_PIX
        if (auto* dq = rhi::dx12::get_queue(q)) { PIXBeginEvent(dq, color, name ? name : ""); return; }
#endif
		(void)q; (void)color; (void)name; // TODO: Vulkan queue markers?
    }

    inline void End(Queue q) noexcept {
#if RHI_ENABLE_PIX
        if (auto* dq = rhi::dx12::get_queue(q)) { PIXEndEvent(dq); return; }
#endif
        (void)q;
    }

    inline void Marker(Queue q, Color color, const char* name) noexcept {
#if RHI_ENABLE_PIX
        if (auto* dq = rhi::dx12::get_queue(q)) { PIXSetMarker(dq, color, name ? name : ""); return; }
#endif
        (void)q; (void)color; (void)name;
    }

} // namespace rhi::debug
