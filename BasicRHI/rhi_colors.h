// rhi_colors.h
#pragma once
#include <cstdint>
#include <string_view>

namespace rhi::colors {

    // --- Canonical color type (RGBA8 packed as 0xAARRGGBB)
    using RGBA8 = std::uint32_t;

    constexpr RGBA8 make(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) noexcept {
        return (RGBA8(a) << 24) | (RGBA8(r) << 16) | (RGBA8(g) << 8) | RGBA8(b);
    }
    constexpr std::uint8_t r(RGBA8 c) noexcept { return std::uint8_t((c >> 16) & 0xFF); }
    constexpr std::uint8_t g(RGBA8 c) noexcept { return std::uint8_t((c >> 8) & 0xFF); }
    constexpr std::uint8_t b(RGBA8 c) noexcept { return std::uint8_t((c >> 0) & 0xFF); }
    constexpr std::uint8_t a(RGBA8 c) noexcept { return std::uint8_t((c >> 24) & 0xFF); }

    // --- Nice named colors (curated for legibility in profilers)
    inline constexpr RGBA8 Transparent = make(0, 0, 0, 0);
    inline constexpr RGBA8 Black = make(0, 0, 0);
    inline constexpr RGBA8 White = make(255, 255, 255);
    inline constexpr RGBA8 Gray = make(128, 128, 128);
    inline constexpr RGBA8 LightGray = make(200, 200, 200);
    inline constexpr RGBA8 DarkGray = make(64, 64, 64);

    inline constexpr RGBA8 Red = make(231, 76, 60);
    inline constexpr RGBA8 Orange = make(243, 156, 18);
    inline constexpr RGBA8 Amber = make(255, 191, 0);
    inline constexpr RGBA8 Yellow = make(241, 196, 15);
    inline constexpr RGBA8 Green = make(46, 204, 113);
    inline constexpr RGBA8 Mint = make(26, 188, 156);
    inline constexpr RGBA8 Teal = make(0, 150, 136);
    inline constexpr RGBA8 Cyan = make(52, 172, 224);
    inline constexpr RGBA8 Blue = make(66, 133, 244);
    inline constexpr RGBA8 Indigo = make(63, 81, 181);
    inline constexpr RGBA8 Purple = make(171, 71, 188);
    inline constexpr RGBA8 Magenta = make(214, 69, 151);
    inline constexpr RGBA8 Pink = make(236, 64, 122);
    inline constexpr RGBA8 Brown = make(121, 85, 72);

    // --- Suggested semantic colors for common GPU domains
    inline constexpr RGBA8 GraphicsPass = Blue;
    inline constexpr RGBA8 ComputePass = Purple;
    inline constexpr RGBA8 CopyPass = Gray;
    inline constexpr RGBA8 RayTracing = Indigo;

    inline constexpr RGBA8 FrameSetup = Teal;
    inline constexpr RGBA8 GBuffer = Orange;
    inline constexpr RGBA8 Shadow = DarkGray;
    inline constexpr RGBA8 Lighting = Yellow;
    inline constexpr RGBA8 PostProcess = Magenta;
    inline constexpr RGBA8 UI = Mint;

    inline constexpr RGBA8 Upload = Cyan;
    inline constexpr RGBA8 Download = Amber;
    inline constexpr RGBA8 Streaming = Brown;

    // A high-contrast palette for auto-assignment
    inline constexpr RGBA8 Palette10[] = {
        Blue, Orange, Green, Red, Purple, Cyan, Magenta, Teal, Amber, Indigo
    };
    inline constexpr std::size_t Palette10Count = sizeof(Palette10) / sizeof(Palette10[0]);


    // Pick a stable color from a string (FNV-1a hash -> palette index)
    inline RGBA8 from_name(std::string_view name) noexcept {
        std::uint32_t h = 2166136261u;
        for (unsigned char c : name) {
            h ^= c;
            h *= 16777619u;
        }
        return Palette10[h % Palette10Count];
    }

    // Slightly brighten/darken (linear in 0..255)
    inline RGBA8 lighten(RGBA8 c, float amount/*-1..+1*/) noexcept {
        auto clamp = [](int v) { return (v < 0) ? 0 : (v > 255 ? 255 : v); };
        const float f = amount;
        int rr = clamp(int(r(c) + 255.0f * f));
        int gg = clamp(int(g(c) + 255.0f * f));
        int bb = clamp(int(b(c) + 255.0f * f));
        return make(std::uint8_t(rr), std::uint8_t(gg), std::uint8_t(bb), a(c));
    }

    inline RGBA8 with_alpha(RGBA8 c, std::uint8_t alpha) noexcept {
        return (c & 0x00FFFFFFu) | (RGBA8(alpha) << 24);
    }

} // namespace rhi::colors
